/* -*- linux-c -*-
 *
 * relay.c - staprun relayfs functions
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Copyright (C) 2007-2023 Red Hat Inc.
 */

#include "staprun.h"
#include <string.h>
#ifdef HAVE_STDATOMIC_H
#include <stdatomic.h>
#endif
#define NDEBUG
#include "gheap.h"


int out_fd[MAX_NR_CPUS];
int monitor_end = 0;
static pthread_t reader[MAX_NR_CPUS];
static int relay_fd[MAX_NR_CPUS]; // fd to kernel per-cpu relayfs
static int avail_cpus[MAX_NR_CPUS];
static volatile sig_atomic_t sigusr2_count; // number of SIGUSR2's received by process
static int sigusr2_processed[MAX_NR_CPUS]; // each thread's count of processed SIGUSR2's
static int bulkmode = 0;
static volatile int stop_threads = 0; // set during relayfs_close to signal threads to die
static time_t *time_backlog[MAX_NR_CPUS];
static int backlog_order=0;
#define BACKLOG_MASK ((1 << backlog_order) - 1)
#define MONITORLINELENGTH 4096

#ifdef NEED_PPOLL
int ppoll(struct pollfd *fds, nfds_t nfds,
	  const struct timespec *timeout, const sigset_t *sigmask)
{
	sigset_t origmask;
	int ready;
	int tim;
	if (timeout == NULL)
		tim = -1;
	else
		tim = timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000;

	sigprocmask(SIG_SETMASK, sigmask, &origmask);
	ready = poll(fds, nfds, tim);
	sigprocmask(SIG_SETMASK, &origmask, NULL);
	return ready;
}
#endif

int init_backlog(int cpu)
{
	int order = 0;
	if (!fnum_max)
		return 0;
	while (fnum_max > 1<<order) order++;
	time_backlog[cpu] = (time_t *)calloc(1<<order, sizeof(time_t));
	if (time_backlog[cpu] == NULL) {
		_err("Memory allocation failed\n");
		return -1;
	}
	backlog_order = order;
	return 0;
}

void write_backlog(int cpu, int fnum, time_t t)
{
	time_backlog[cpu][fnum & BACKLOG_MASK] = t;
}

time_t read_backlog(int cpu, int fnum)
{
	return time_backlog[cpu][fnum & BACKLOG_MASK];
}

static int open_outfile(int fnum, int cpu, int remove_file)
{
	char buf[PATH_MAX];
	time_t t;
	if (!outfile_name) {
		_err("-S is set without -o. Please file a bug report.\n");
		return -1;
	}

	time(&t);
	if (fnum_max) {
		if (remove_file) {
			 /* remove oldest file */
			if (make_outfile_name(buf, PATH_MAX, fnum - fnum_max,
				 cpu, read_backlog(cpu, fnum - fnum_max),
				 bulkmode) < 0)
				return -1;
			remove(buf); /* don't care */
		}
		write_backlog(cpu, fnum, t);
	}

	if (make_outfile_name(buf, PATH_MAX, fnum, cpu, t, bulkmode) < 0)
		return -1;
	out_fd[cpu] = open_cloexec (buf, O_CREAT|O_TRUNC|O_WRONLY, 0666);
	if (out_fd[cpu] < 0) {
		perr("Couldn't open output file %s", buf);
		return -1;
	}
	return 0;
}

static int switch_outfile(int cpu, int *fnum)
{
	int remove_file = 0;

	dbug(3, "thread %d switching file\n", cpu);
	close(out_fd[cpu]);
	*fnum += 1;
	if (fnum_max && *fnum >= fnum_max)
		remove_file = 1;
	if (open_outfile(*fnum, cpu, remove_file) < 0) {
		perr("Couldn't open file for cpu %d, exiting.", cpu);
		return -1;
	}
	return 0;
}



/* In serialized (non-bulk) output mode, ndividual messages that have
 been received from the kernel per-cpu relays are stored in an central
 serializing data structure - in this case, a heap.  They are ordered
 by message sequence number.  An additional thread (serializer_thread)
 scans & sequences the output. */
struct serialized_message {
        union {
                struct _stp_trace bufhdr;
                char bufhdr_raw[sizeof(struct _stp_trace)];
        };
        time_t received; // timestamp when this message was enqueued
        char *buf; // malloc()'d size >= rounded(bufhdr.pdu_len)
};
static struct serialized_message* buffer_heap = NULL; // the heap

// NB: we control memory via realloc(), gheap just manipulates entries in place
static unsigned buffer_heap_size = 0; // used number of entries 
static unsigned buffer_heap_alloc = 0; // allocation length, always >= buffer_heap_size
static unsigned last_sequence_number = 0; // last processed sequential message number

#ifdef HAVE_STDATOMIC_H
static atomic_ulong lost_message_count = 0; // how many sequence numbers we know we missed
static atomic_ulong lost_byte_count = 0; // how many bytes were skipped during resync
#else
static unsigned long lost_message_count = 0; // how many sequence numbers we know we missed
static unsigned long lost_byte_count = 0; // how many bytes were skipped during resync
#endif

// concurrency control for the buffer_heap
static pthread_cond_t buffer_heap_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t buffer_heap_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t serializer_thread; // ! bulkmode only


static void buffer_heap_mover (void *const dest, const void *const src)
{
        memmove (dest, src, sizeof(struct serialized_message));
}

// NB: since we want to sort messages into an INCREASING heap sequence,
// we reverse the normal comparison operator.  gheap_pop_heap() should
// therefore return the SMALLEST element.
static int buffer_heap_comparer (const void *const ctx, const void *a, const void *b)
{
        (void) ctx;
        uint32_t aa = ((struct serialized_message *)a)->bufhdr.sequence;
        uint32_t bb = ((struct serialized_message *)b)->bufhdr.sequence;
        return (aa > bb);
}        

static const struct gheap_ctx buffer_heap_ctx = {
        .item_size = sizeof(struct serialized_message),
        .less_comparer = buffer_heap_comparer,
        .item_mover = buffer_heap_mover,
        .page_chunks = 16, // arbitrary
        .fanout = 2 // standard binary heap
};


#define MAX_MESSAGE_LENGTH (128*1024) /* maximum likely length of a single pdu */



/* Thread that reads per-cpu messages, and stuffs complete ones into
   dynamically allocated serialized_message nodes in a binary tree. */
static void* reader_thread_serialmode (void *data)
{
        int rc, cpu = (int)(long)data;
        struct pollfd pollfd;
	sigset_t sigs;
	cpu_set_t cpu_mask;
                
	sigemptyset(&sigs);
	sigaddset(&sigs,SIGUSR2);
	pthread_sigmask(SIG_BLOCK, &sigs, NULL);

	sigfillset(&sigs);
	sigdelset(&sigs,SIGUSR2);

	CPU_ZERO(&cpu_mask);
	CPU_SET(cpu, &cpu_mask);
	if( sched_setaffinity( 0, sizeof(cpu_mask), &cpu_mask ) < 0 )
		_perr("sched_setaffinity");

	pollfd.fd = relay_fd[cpu];
	pollfd.events = POLLIN;

        while (! stop_threads) {
                // read a message header
                struct serialized_message message;
                
                /* 200ms, close to human level of "instant" */
                struct timespec tim, *timeout = &tim;
                timeout->tv_sec = reader_timeout_ms / 1000;
                timeout->tv_nsec = (reader_timeout_ms - timeout->tv_sec * 1000) * 1000000;
                
                rc = ppoll(&pollfd, 1, timeout, &sigs);
                if (rc < 0) {
			dbug(3, "cpu=%d poll=%d errno=%d\n", cpu, rc, errno);
			if (errno == EINTR) {
				if (stop_threads)
					break;
			} else {
				_perr("poll error");
				goto error_out;
			}
                }

                // set the timestamp
                message.received = time(NULL);

                /* Read the header. */
                rc = read(relay_fd[cpu], &message.bufhdr, sizeof(message.bufhdr));
                if (rc <= 0) /* seen during normal shutdown or error */
                        continue;
                if (rc != sizeof(message.bufhdr)) {
                        lost_byte_count += rc;
                        continue;
                }

                /* Validate the magic value.  In case of mismatch,
                   keep on reading & shifting the header, one byte at
                   a time, until we get a match. */
                while (! stop_threads && memcmp(message.bufhdr.magic, STAP_TRACE_MAGIC, 4)) {
                        /* Do not count padding bytes */
                        if ((* (&message.bufhdr_raw[0])) != '\0')
                           lost_byte_count ++;
                        memmove(& message.bufhdr_raw[0],
                                & message.bufhdr_raw[1],
                                sizeof(message.bufhdr_raw)-1);
                        rc = read(relay_fd[cpu],
                                  &message.bufhdr_raw[sizeof(message.bufhdr_raw)-1],
                                  1);
                        if (rc <= 0) /* seen during normal shutdown or error */
                                break;
                }

                /* Validate it slightly.  Because of lost messages, we might be getting
                   not a proper _stp_trace struct but the interior of some piece of 
                   trace text message.  XXX: validate bufhdr.sequence a little bit too? */
                if (message.bufhdr.pdu_len == 0 ||
                    message.bufhdr.pdu_len > MAX_MESSAGE_LENGTH) {
                        lost_byte_count += sizeof(message.bufhdr);
                        continue;
                }

                // Allocate the pdu body
                message.buf = malloc(message.bufhdr.pdu_len);
                if (message.buf == NULL)
                {
                        lost_byte_count += message.bufhdr.pdu_len;
                        continue;
                }
                
                /* Read the message, perhaps in pieces (such as if crossing
                 * relayfs subbuf boundaries). */
                size_t bufread = 0;
                while (bufread < message.bufhdr.pdu_len) {
                        rc = read(relay_fd[cpu], message.buf+bufread, message.bufhdr.pdu_len-bufread);
                        if (rc <= 0) {
                                lost_byte_count += message.bufhdr.pdu_len-bufread;
                                break; /* still process it; hope we can resync at next packet. */
                        }
                        bufread += rc;
                }

                // plop the message into the buffer_heap
                pthread_mutex_lock(& buffer_heap_mutex);
                if (message.bufhdr.sequence < last_sequence_number) {
                        // whoa! is this some old message that we've assumed lost?
                        // or are we wrapping around the uint_32 sequence numbers?
                        _perr("unexpected sequence=%u", message.bufhdr.sequence);
                }
                        
                // is it large enough?  if not, realloc
                if (buffer_heap_alloc - buffer_heap_size == 0) { // full
                        unsigned new_buffer_heap_alloc = (buffer_heap_alloc + 1) * 1.5;
                        struct serialized_message *new_buffer_heap =
                                realloc(buffer_heap,
                                        new_buffer_heap_alloc * sizeof(struct serialized_message));
                        if (new_buffer_heap == NULL) {
                                _perr("out of memory while enlarging buffer heap");
                                free (message.buf);
                                lost_message_count ++;
                                pthread_mutex_unlock(& buffer_heap_mutex);
                                continue;
                        }
                        buffer_heap = new_buffer_heap;
                        buffer_heap_alloc = new_buffer_heap_alloc;
                }
                // plop copy of message struct into slot at end of heap
                buffer_heap[buffer_heap_size++] = message;
                // push it into heap
                gheap_push_heap(&buffer_heap_ctx,
                                buffer_heap,
                                buffer_heap_size);
                // and c'est tout
                pthread_mutex_unlock(& buffer_heap_mutex);
                pthread_cond_broadcast (& buffer_heap_cond);
                dbug(3, "thread %d received seq=%u\n", cpu, message.bufhdr.sequence);
        }

	dbug(3, "exiting thread for cpu %d\n", cpu);
        return NULL;
        
error_out:
	/* Signal the main thread that we need to quit */
	kill(getpid(), SIGTERM);
	dbug(2, "exiting thread for cpu %d after error\n", cpu);
        
        return NULL;
}


// Print and free buffer of given serialized message.
static void print_serialized_message (struct serialized_message *msg)
{
        // check if file switching is necessary, as per staprun -S

        // NB: unlike reader_thread_bulkmode(), we don't need to use
        // mutexes to protect switch_file[] or such, because we're the
        // ONLY thread doing output.
        unsigned cpu = 0; // arbitrary
        static ssize_t wsize = 0; // how many bytes we've written into the serialized file so far
        static int fnum = 0; // which file number we're using

        if ((fsize_max && (wsize > fsize_max)) ||
            (sigusr2_count > sigusr2_processed[cpu])) {
                dbug(2, "switching output file wsize=%ld fsize_max=%ld sigusr2 %d > %d\n",
                     wsize, fsize_max, sigusr2_count, sigusr2_processed[cpu]);
                sigusr2_processed[cpu] = sigusr2_count;                
                if (switch_outfile(cpu, &fnum) < 0) {
                        perr("unable to switch output file");
                        // but continue
                }
                wsize = 0;
        }

        
        // write loop ... could block if e.g. the output disk is slow
        // or the user hits a ^S (XOFF) on the tty
        ssize_t sent = 0;
        do {
                ssize_t ret = write (out_fd[avail_cpus[0]],
                                     msg->buf+sent, msg->bufhdr.pdu_len-sent);
                if (ret <= 0) {
                        perr("error writing output");
                        break;
                }
                sent += ret;
        } while ((unsigned)sent < msg->bufhdr.pdu_len);
        wsize += sent;
        
        // free the associated buffer
        free (msg->buf);
        msg->buf = NULL;
}


/* Thread that checks on the heap of messages, and pumps them out to
   the designated output fd in sequence.  It waits, but only a little
   while, if it has only fresher messages than it's expecting.  It
   exits upon a global stop_threads indication.
*/
static void* reader_thread_serializer (void *data) {
        (void) data;
        while (! stop_threads) {
                /* timeout 0-1 seconds; this is the maximum extra time that
                   stapio will be waiting after a ^C */
                struct timespec ts = {.tv_sec=time(NULL)+1, .tv_nsec=0};
                int rc;
                pthread_mutex_lock(& buffer_heap_mutex);                
                rc = pthread_cond_timedwait (& buffer_heap_cond,
                                             & buffer_heap_mutex,
                                             & ts);

		dbug(3, "serializer cond wait rc=%d heapsize=%u\n", rc, buffer_heap_size);
                time_t now = time(NULL);
                unsigned processed = 0;
                while (buffer_heap_size > 0) { // consume as much as possible
                        // check out the sequence# of the first element
                        uint32_t buf_min_seq = buffer_heap[0].bufhdr.sequence;

                        dbug(3, "serializer last=%u seq=%u\n", last_sequence_number, buf_min_seq);
                                
                        if ((buf_min_seq == last_sequence_number + 1) || // expected seq#
                            (buffer_heap[0].received + 2 <= now)) {  // message too old
                                // "we've got one!" -- or waited too long for one
                                // get it off the head of the heap
                                gheap_pop_heap(&buffer_heap_ctx,
                                               buffer_heap,
                                               buffer_heap_size);
                                buffer_heap_size --; // becomes index where the head was moved
                                processed ++;

                                // take a copy of the whole message
                                struct serialized_message msg = buffer_heap[buffer_heap_size];
                                
                                // paranoid clear this field of the now-unused slot
                                buffer_heap[buffer_heap_size].buf = NULL;
                                // update statistics
                                if (attach_mod == 1 && last_sequence_number == 0) // first message after staprun -A
                                        ; // do not penalize it with lost messages
                                else
                                        lost_message_count += (buf_min_seq - last_sequence_number - 1);
                                last_sequence_number = buf_min_seq;
                                
                                // unlock the mutex, permitting
                                // reader_thread_serialmode threads to
                                // resume piling messages into the
                                // heap while we print stuff
                                pthread_mutex_unlock(& buffer_heap_mutex);

                                print_serialized_message (& msg);
                                
                                // must re-take lock for next iteration of the while loop
                                pthread_mutex_lock(& buffer_heap_mutex);
                        } else {
                                // processed as much of the heap as we
                                // could this time; wait for the
                                // condition again
                                break;
                        }
                }
                pthread_mutex_unlock(& buffer_heap_mutex);
                if (processed > 0)
                        dbug(2, "serializer processed n=%u\n", processed);
        }
        return NULL;        
}



// At the end of the program main loop, flush out any the remaining
// messages and free up all that heapy data.
static void reader_serialized_flush()
{
        dbug(3, "serializer flushing messages=%u\n", buffer_heap_size);
        while (buffer_heap_size > 0) { // consume it all
                // check out the sequence# of the first element
                uint32_t buf_min_seq = buffer_heap[0].bufhdr.sequence;
                dbug(3, "serializer seq=%u\n", buf_min_seq);
                gheap_pop_heap(&buffer_heap_ctx,
                               buffer_heap,
                               buffer_heap_size);
                buffer_heap_size --; // also index where the head was moved

                // NB: no need for mutex manipulations, this is super single threaded
                print_serialized_message (& buffer_heap[buffer_heap_size]);

                lost_message_count += (buf_min_seq - last_sequence_number - 1);
                last_sequence_number = buf_min_seq;                
        }
        free (buffer_heap);
        buffer_heap = NULL;
}



/**
 *	reader_thread - per-cpu channel buffer reader, bulkmode (one output file per cpu input file)
 */
static void *reader_thread_bulkmode (void *data)
{
        char buf[MAX_MESSAGE_LENGTH];
        struct _stp_trace bufhdr;

        int rc, cpu = (int)(long)data;
        struct pollfd pollfd;
	sigset_t sigs;
	off_t wsize = 0;
	int fnum = 0;
	cpu_set_t cpu_mask;
                
	sigemptyset(&sigs);
	sigaddset(&sigs,SIGUSR2);
	pthread_sigmask(SIG_BLOCK, &sigs, NULL);

	sigfillset(&sigs);
	sigdelset(&sigs,SIGUSR2);

	CPU_ZERO(&cpu_mask);
	CPU_SET(cpu, &cpu_mask);
	if( sched_setaffinity( 0, sizeof(cpu_mask), &cpu_mask ) < 0 )
		_perr("sched_setaffinity");

	pollfd.fd = relay_fd[cpu];
	pollfd.events = POLLIN;

        do {
                /* 200ms, close to human level of "instant" */
                struct timespec tim, *timeout = &tim;
                timeout->tv_sec = reader_timeout_ms / 1000;
                timeout->tv_nsec = (reader_timeout_ms - timeout->tv_sec * 1000) * 1000000;
                
                rc = ppoll(&pollfd, 1, timeout, &sigs);
                if (rc < 0) {
			dbug(3, "cpu=%d poll=%d errno=%d\n", cpu, rc, errno);
			if (errno == EINTR) {
				if (stop_threads)
					break;

                                if (sigusr2_count > sigusr2_processed[cpu]) {
                                       sigusr2_processed[cpu] = sigusr2_count;
                                       if (switch_outfile(cpu, &fnum) < 0) {
						goto error_out;
                                       }
                                       wsize = 0;
				}
			} else {
				_perr("poll error");
				goto error_out;
			}
                }

                /* Read the header. */
                rc = read(relay_fd[cpu], &bufhdr, sizeof(bufhdr));
                if (rc <= 0) /* seen during normal shutdown */
                        continue;
                if (rc != sizeof(bufhdr)) {
                        _perr("bufhdr read error, attempting resync");
                        rc = read(relay_fd[cpu], buf, sizeof(buf)); /* drain the buffers */
                        (void) rc;
                        continue;
                }

                /* Validate it slightly.  Because of lost messages, we might be getting
                   not a proper _stp_trace struct but the interior of some piece of 
                   trace text message.  XXX: validate bufhdr.sequence a little bit too? */
                if (bufhdr.pdu_len == 0 || bufhdr.pdu_len > sizeof(buf)) {
                        /* _perr("bufhdr corrupt, attempting resync"); */ 
                        rc = read(relay_fd[cpu], buf, sizeof(buf)); /* drain the buffers */
                        (void) rc;
                        continue; /* may resync at next subbuf boundary so don't give up */
                }

                /* Read the message, perhaps in pieces (such as if crossing
                 * relayfs subbuf boundaries). */
                size_t bufread = 0;
                while (bufread < bufhdr.pdu_len) {
                        rc = read(relay_fd[cpu], buf+bufread, bufhdr.pdu_len-bufread);
                        if (rc <= 0) {
                                /* _perr("partial message received"); */
                                break; /* still process it; hope we can resync next time. */
                        }
                        bufread += rc;
                }

                int wbytes = rc;
                char *wbuf = buf;

                dbug(3, "cpu %d: read %d bytes of data\n", cpu, rc);

                /* Switching file */
                if ((fsize_max && ((wsize + rc) > fsize_max)) ||
                    (sigusr2_count > sigusr2_processed[cpu])) {
                        sigusr2_processed[cpu] = sigusr2_count;
                        if (switch_outfile(cpu, &fnum) < 0) {
                                goto error_out;
                        }
                        wsize = 0;
                }

                /* Copy loop.  Must repeat write(2) in case of a pipe overflow
                   or other transient fullness. */
                while (wbytes > 0) {
                        if (monitor) {
                                ssize_t bytes = wbytes > MONITORLINELENGTH ? MONITORLINELENGTH : wbytes;
                                /* Start scanning the wbuf[] for lines - \n.
                                   Plop each one found into the h_queue.lines[] ring. */
                                char *p = wbuf; /* scan position */
                                char *p_end = wbuf + bytes; /* one past last byte */
                                char *line = p;
                                while (p < p_end) {
                                        if (*p == '\n') { /* got a line */
                                                monitor_remember_output_line(line, (p-line)+1); /* strlen, including \n */
                                                line = p+1;
                                        }
                                        p++;
                                }
                                /* Flush remaining output */
                                if (line != p_end)
                                        monitor_remember_output_line(line, (p_end - line));
                                wbytes -= bytes;
                                wbuf += bytes;
                                wsize += bytes;
                        } else {
                                int fd;
                                /* Only bulkmode and fsize_max use per-cpu output files. Otherwise,
                                   there's just a single output fd stored at out_fd[avail_cpus[0]]. */
                                fd = out_fd[cpu];
                                rc = write(fd, &bufhdr, sizeof(bufhdr)); // write header
                                rc |= write(fd, wbuf, wbytes); // write payload
                                if (rc <= 0) {
                                        perr("Couldn't write to output %d for cpu %d, exiting.",
                                             fd, cpu);
                                        goto error_out;
                                }
                                wbytes -= rc;
                                wbuf += rc;
                                wsize += rc;
                        }
                }

        } while (!stop_threads);
	dbug(3, "exiting thread for cpu %d\n", cpu);
	return(NULL);

error_out:
	/* Signal the main thread that we need to quit */
	kill(getpid(), SIGTERM);
	dbug(2, "exiting thread for cpu %d after error\n", cpu);
	return(NULL);
}


static void switchfile_handler(int sig)
{
        (void) sig;
	if (stop_threads || !outfile_name)
		return;
        sigusr2_count ++;
}


/**
 *	init_relayfs - create files and threads for relayfs processing
 *
 *	Returns 0 if successful, negative otherwise
 */
int init_relayfs(void)
{
	int i, len;
	int cpui = 0;
	char rqbuf[128];
        char buf[PATH_MAX];
        struct sigaction sa;

	dbug(2, "initializing relayfs\n");

	reader[0] = (pthread_t)0;
	relay_fd[0] = 0;
	out_fd[0] = 0;

        /* Find out whether probe module was compiled with STP_BULKMODE. */
	if (send_request(STP_BULK, rqbuf, sizeof(rqbuf)) == 0)
		bulkmode = 1;

	/* Try to open a slew of per-cpu trace%d files.  Per PR19241,
	   we need to go through all potentially present CPUs up to
	   get_nprocs_conf(), up to MAX_NR_CPUS (used for array
	   allocations).  For !bulknode, "trace0" will be typically
	   used, prior to systemtap 4.5; after, all are used. */

        int nprocs = get_nprocs_conf();
        if (nprocs > MAX_NR_CPUS) {
                err("Too many CPUs: get_nprocs_conf()=%d vs MAX_NR_CPUS=%d\n", nprocs, MAX_NR_CPUS);
                return -1;
        }
        
	for (i = 0; i < nprocs; i++) {
                relay_fd[i] = -1;

#ifdef HAVE_OPENAT
                if (relay_basedir_fd >= 0) {
                        if (sprintf_chk(buf, "trace%d", i))
                                return -1;
                        dbug(2, "attempting to openat %s\n", buf);
                        relay_fd[i] = openat_cloexec(relay_basedir_fd, buf, O_RDONLY | O_NONBLOCK, 0);
                }
#endif
                if (relay_fd[i] < 0) {
                        if (sprintf_chk(buf, "/sys/kernel/debug/systemtap/%s/trace%d",
                                        modname, i))
                                return -1;
                        dbug(2, "attempting to open %s\n", buf);
                        relay_fd[i] = open_cloexec(buf, O_RDONLY | O_NONBLOCK, 0);
                }
                if (relay_fd[i] < 0) {
                        if (sprintf_chk(buf, "/proc/systemtap/%s/trace%d",
                                        modname, i))
                                return -1;
                        dbug(2, "attempting to open %s\n", buf);
                        relay_fd[i] = open_cloexec(buf, O_RDONLY | O_NONBLOCK, 0);
                }
		if (relay_fd[i] >= 0) {
			avail_cpus[cpui++] = i;
		}
	}
	ncpus = cpui;
        /* ncpus could be smaller than nprocs if some cpus are offline */
	dbug(2, "ncpus=%d, nprocs=%d, bulkmode=%d\n", ncpus, nprocs, bulkmode);
	for (i = 0; i < ncpus; i++)
		dbug(2, "cpui=%d, relayfd=%d\n", i, avail_cpus[i]);

	if (ncpus == 0) {
		_err("couldn't open %s: %s\n", buf, strerror(errno));
		return -1;
	}

        /* PR7097 */
        if (load_only)
                return 0;

	if (fsize_max) {
		/* switch file mode */
		for (i = 0; i < ncpus; i++) {
			if (init_backlog(avail_cpus[i]) < 0)
				return -1;
			if (open_outfile(0, avail_cpus[i], 0) < 0)
  				return -1;
		}
	} else if (bulkmode) {
		for (i = 0; i < ncpus; i++) {
			if (outfile_name) {
				/* special case: for testing we sometimes want to write to /dev/null */
				if (strcmp(outfile_name, "/dev/null") == 0) {
					/* This strcpy() is OK, since
					 * we know buf is PATH_MAX
					 * bytes long. */
					strcpy(buf, "/dev/null");
				} else {
					len = stap_strfloctime(buf, PATH_MAX,
						 outfile_name, time(NULL));
					if (len < 0) {
						err("Invalid FILE name format\n");
						return -1;
					}
					if (snprintf_chk(&buf[len],
						PATH_MAX - len, "_%d", avail_cpus[i]))
						return -1;
				}
			} else {
				if (sprintf_chk(buf, "stpd_cpu%d", avail_cpus[i]))
					return -1;
			}

			out_fd[avail_cpus[i]] = open_cloexec (buf, O_CREAT|O_TRUNC|O_WRONLY, 0666);
			if (out_fd[avail_cpus[i]] < 0) {
				perr("Couldn't open output file %s", buf);
				return -1;
			}
		}
	} else {
		/* stream mode */
		if (outfile_name) {
			len = stap_strfloctime(buf, PATH_MAX,
						 outfile_name, time(NULL));
			if (len < 0) {
				err("Invalid FILE name format\n");
				return -1;
			}
			out_fd[avail_cpus[0]] = open_cloexec (buf, O_CREAT|O_TRUNC|O_WRONLY, 0666);
			if (out_fd[avail_cpus[0]] < 0) {
				perr("Couldn't open output file %s", buf);
				return -1;
			}
		} else
			out_fd[avail_cpus[0]] = STDOUT_FILENO;
	}

        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = switchfile_handler;
        sa.sa_flags = 0;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR2, &sa, NULL);

        dbug(2, "starting threads\n");
        for (i = 0; i < ncpus; i++) {
                if (pthread_create(&reader[avail_cpus[i]], NULL,
                                   bulkmode ? reader_thread_bulkmode : reader_thread_serialmode,
                                   (void *)(long)avail_cpus[i]) < 0) {
                        _perr("failed to create thread");
                        return -1;
                }
        }
        if (! bulkmode)
                if (pthread_create(&serializer_thread, NULL,
                                   reader_thread_serializer, NULL) < 0) {
                        _perr("failed to create thread");
                        return -1;
                }

	return 0;
}

void close_relayfs(void)
{
	int i;
	usleep(reader_timeout_ms*2*1000); /* PR31597, delay to drain buffers */
	stop_threads = 1;
	dbug(2, "closing\n");

	for (i = 0; i < ncpus; i++) {
		if (reader[avail_cpus[i]])
			pthread_join(reader[avail_cpus[i]], NULL);
		else
			break;
	}
        if (! bulkmode) {
                if (serializer_thread) // =0 on load_only!
                        pthread_join(serializer_thread, NULL);
                // at this point, we know all reader and writer
                // threads for the buffer_heap are dead.
                reader_serialized_flush();

                if (lost_message_count > 0 || lost_byte_count > 0)
                        eprintf("WARNING: There were %u lost messages and %u lost bytes.\n",
                                lost_message_count, lost_byte_count); 
        }

	for (i = 0; i < ncpus; i++) {
		if (relay_fd[avail_cpus[i]] >= 0)
			close(relay_fd[avail_cpus[i]]);
		else
			break;
	}
	dbug(2, "done\n");
}

void kill_relayfs(void)
{
	int i;
	stop_threads = 1;
	dbug(2, "killing\n");
	for (i = 0; i < ncpus; i++) {
		if (reader[avail_cpus[i]])
			pthread_cancel(reader[avail_cpus[i]]); /* no wait */
		else
			break;
	}
	for (i = 0; i < ncpus; i++) {
		if (relay_fd[avail_cpus[i]] >= 0)
			close(relay_fd[avail_cpus[i]]);
		else
			break;
	}
	dbug(2, "done\n");
}
