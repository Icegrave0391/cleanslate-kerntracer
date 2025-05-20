/* -*- linux-c -*- 
 * Print Flush Function
 * Copyright (C) 2007-2008 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

/** Send the print buffer to the transport now.
 * Output accumulates in the print buffer until it
 * is filled, or this is called. This MUST be called before returning
 * from a probe or accumulated output in the print buffer will be lost.
 *
 * @note Interrupts must be disabled to use this.
 */

static void __stp_print_flush(struct _stp_log *log)
{
	char *bufp = log->buf; /* next byte of log->buf left to write */
	size_t len = log->len; /* # bytes of log->buf left to write */
        const size_t hlen = sizeof(struct _stp_trace);
	void *entry = NULL; /* current output buf handle */
        size_t bytes_reserved; /* current output buf size available */
        
	/* check to see if there is anything in the buffer */
	if (likely(len == 0))
		return;

	if (unlikely(log->no_flush)) {
		log->is_full = true;
		return;
	}

	log->len = 0; /* clear it for later reuse */
	dbug_trans(1, "len = %zu\n", len);

        /* try to reserve header + len */
        bytes_reserved = _stp_data_write_reserve(hlen+len,
                                                 &entry);
	if (bytes_reserved > 0 && bytes_reserved < hlen){
	  /* Cannot have header straddle sub buffers. */
	  /* Pad out that remainder of the subbuf and try again */
	  memset(entry, 0, bytes_reserved);
	  bytes_reserved = _stp_data_write_reserve(hlen+len,
						   &entry);
	}

        /* require at least header to fit in its entirety */
        if (likely(entry && bytes_reserved >= hlen)) {
                /* copy new _stp_trace_ header */
                struct _stp_trace t = {
                        .sequence = _stp_seq_inc(),
                        .pdu_len = len
                };
                memcpy(&t.magic, STAP_TRACE_MAGIC, 4);
                memcpy(_stp_data_entry_data(entry), &t, hlen);
                /* copy the first part of the message */
                memcpy(_stp_data_entry_data(entry)+hlen,
                       bufp, bytes_reserved-hlen);
                bufp += bytes_reserved-hlen;
                len -= bytes_reserved-hlen;
                /* send header + first part */
                _stp_data_write_commit(entry);
                
                /* loop to copy the rest of the message into subsequent bufs */
                while (len > 0) {
                        bytes_reserved = _stp_data_write_reserve(len, &entry);
                        if (likely(entry && bytes_reserved)) {
                                memcpy(_stp_data_entry_data(entry), bufp,
                                       bytes_reserved);
                                _stp_data_write_commit(entry);
                                bufp += bytes_reserved;
                                len -= bytes_reserved;
                        } else { /* rest of message cannot fit at this time */
                                /* NB: the receiver must somehow resynch the framing! */
                                atomic_inc(&_stp_transport_failures);
                                break;
                        }
                }
        } else {
                atomic_inc(&_stp_transport_failures);
        }
}
