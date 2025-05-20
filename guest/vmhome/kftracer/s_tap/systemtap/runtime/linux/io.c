/* -*- linux-c -*- 
 * I/O for printing warnings, errors and debug messages
 * Copyright (C) 2005-2009 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STAPLINUX_IO_C_
#define _STAPLINUX_IO_C_

/** @file io.c
 * @brief I/O for printing warnings, errors and debug messages.
 */
/** @addtogroup io I/O
 * @{
 */

#define WARN_STRING "WARNING: "
#define ERR_STRING "ERROR: "

enum code { WARN=1, ERROR, DBUG };

static void _stp_vlog (enum code type, const char *func, int line, const char *fmt, va_list args)
        __attribute ((format (printf, 4, 0)));

static void _stp_vlog (enum code type, const char *func, int line, const char *fmt, va_list args)
{
	int prefix_len, msg_len;
	struct _stp_log *log;
	unsigned long flags;
	va_list args_copy;
	char *buf;

	/* Warnings and errors are printed to the control channel */
	switch (type) {
	case WARN:
		_stp_ctl_log_werr(WARN_STRING, sizeof(WARN_STRING) - 1,
				  fmt, args);
		return;
	case ERROR:
		_stp_ctl_log_werr(ERR_STRING, sizeof(ERR_STRING) - 1,
				  fmt, args);
		return;
	case DBUG:
		/* Debug messages are handled below */
		break;
	}

	/*
	 * Calculate the total possible length of the debug message. It's the
	 * length of the prefix plus the length of the message plus one in case
	 * a newline character needs to be appended. Note that we need a copy of
	 * the va_list arguments because the vsnprintf() call will erase them.
	 */
	prefix_len = snprintf(NULL, 0, "%s:%d: ", func, line);
	va_copy(args_copy, args);
	msg_len = vsnprintf(NULL, 0, fmt, args_copy);
	va_end(args_copy);

	if (!_stp_print_trylock_irqsave(&flags))
		return;

	buf = _stp_reserve_bytes(prefix_len + msg_len + 1);
	if (!buf)
		goto err_unlock;

	/*
	 * We can use raw *sprintf() here because the sizes have already been
	 * validated. Additionally, the reserved size accomodates for an extra
	 * byte so there won't be an overflow when the NUL termination is added
	 * by *sprintf(). The NUL termination isn't desired but there isn't any
	 * way to prevent it from being added.
	 */
	sprintf(buf, "%s:%d: ", func, line);
	vsprintf(buf + prefix_len, fmt, args);

	/*
	 * Make sure the last character is a newline. If it already is, then
	 * discard the extra byte that was reserved.
	 */
	if (buf[prefix_len + msg_len - 1] != '\n')
		buf[prefix_len + msg_len] = '\n';
	else
		_stp_unreserve_bytes(1);

	/* Flush the log now so userspace is quickly notified of the message */
	log = per_cpu_ptr(_stp_log_pcpu, raw_smp_processor_id());
	__stp_print_flush(log);
err_unlock:
	_stp_print_unlock_irqrestore(&flags);
}

/** Prints warning.
 * This function sends a warning message immediately to staprun. It
 * will also be sent over the bulk transport (relayfs) if it is
 * being used. If the last character is not a newline, then one 
 * is added. 
 * @param fmt A variable number of args.
 */
static void _stp_warn (const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vlog (WARN, NULL, 0, fmt, args);
	va_end(args);
}

/** Exits and unloads the module.
 * This function sends a signal to staprun to tell it to
 * unload the module and exit. The module will not be 
 * unloaded until after the current probe returns.
 * @note Be careful to not treat this like the Linux exit() 
 * call. You should probably call return immediately after 
 * calling _stp_exit().
 */
static void _stp_exit (void)
{
	/* Just set the flag since this is possibly called from
	   kprobe context. A timer will come along and call
	   _stp_request_exit() for us.  */
	_stp_exit_flag = 1;
}

/** Prints error message and exits.
 * This function sends an error message immediately to staprun. It
 * will also be sent over the bulk transport (relayfs) if it is
 * being used. If the last character is not a newline, then one 
 * is added. 
 *
 * After the error message is displayed, the module will transition
 * to exiting-state (as if ^C was pressed) and will eventually unload.
 * @param fmt A variable number of args.
 * @sa _stp_exit().
 *
 * NB: this function should not be used from script-accessible tapset
 * functions.  Those should simply set CONTEXT->last_error, so that
 * script-level try/catch blocks can handle them.  This is for random
 * runtime internal matters that a script didn't invoke and can't
 * expect to handle.
 */
static void _stp_error (const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vlog (ERROR, NULL, 0, fmt, args);
	va_end(args);
	_stp_exit();
}


/** Prints error message.
 * This function sends an error message immediately to staprun. It
 * will also be sent over the bulk transport (relayfs) if it is
 * being used. If the last character is not a newline, then one 
 * is added. 
 *
 * @param fmt A variable number of args.
 * @sa _stp_error
 */
static void _stp_softerror (const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vlog (ERROR, NULL, 0, fmt, args);
	va_end(args);
}


static void _stp_dbug (const char *func, int line, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vlog (DBUG, func, line, fmt, args);
	va_end(args);
}

/** @} */
#endif /* _STAPLINUX_IO_C_ */
