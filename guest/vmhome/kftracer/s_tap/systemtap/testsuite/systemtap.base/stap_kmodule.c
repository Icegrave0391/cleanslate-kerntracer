/* -*- linux-c -*- 
 * Systemtap Test Module 1
 * Copyright (C) 2007 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

/*
 * The purpose of this module is to provide a function that can be
 * triggered from user context via a /proc file.  Systemtap scripts
 * set probes on the function and run tests to see if the expected
 * output is received. This is better than using the kernel's modules
 * because kernel internals frequently change.
 */

/************ Below are the functions to create this module ************/

struct timer_list simple_timer;
static const int timer_interval = 5;

static void simple_timer_function(struct timer_list *timer)
{
	static int count;
	mod_timer (&simple_timer, jiffies + ( msecs_to_jiffies(timer_interval)));
	if (count) {
	  count = 0;
	}
	else {
	  count = 1;
	}
}

int init_module(void)
{
	timer_setup (&simple_timer, simple_timer_function,0);
	mod_timer (&simple_timer, jiffies + msecs_to_jiffies(timer_interval));
	return 0;
}

void cleanup_module(void)
{
	del_timer (&simple_timer);
}

MODULE_DESCRIPTION("systemtap test module");
MODULE_LICENSE("GPL");
