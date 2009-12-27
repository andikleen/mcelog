/* Copyright (C) 2009 Intel Corporation 
   Author: Andi Kleen
   Event loop for mcelog daemon mode.

   mcelog is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; version
   2.

   mcelog is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should find a copy of v2 of the GNU General Public License somewhere
   on your Linux system; if not, write to the Free Software Foundation, 
   Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA */
#define _GNU_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/fcntl.h>
#include <signal.h>
#include "mcelog.h"
#include "eventloop.h"

#define MAX_POLLFD 10

static int max_pollfd;

struct pollcb { 
	poll_cb_t cb;
	int fd;
	void *data;
};

static struct pollfd pollfds[MAX_POLLFD];
static struct pollcb pollcbs[MAX_POLLFD];	

static sigset_t event_sigs;

static int closeonexec(int fd)
{
	int flags = fcntl(fd, F_GETFD);
	if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) { 
		SYSERRprintf("Cannot set FD_CLOEXEC flag on fd");
		return -1;
	}
	return 0;
}

int register_pollcb(int fd, int events, poll_cb_t cb, void *data)
{
	int i = max_pollfd;

	if (closeonexec(fd) < 0)
		return -1;

	if (i >= MAX_POLLFD) {
		Eprintf("poll table overflow");
		return -1;
	}
	max_pollfd++;

	pollfds[i].fd = fd;
	pollfds[i].events = events;
	pollcbs[i].cb = cb;
	pollcbs[i].data = data;
	return 0;
}

/* Could mark free and put into a free list */
void unregister_pollcb(struct pollfd *pfd)
{
	int i = pfd - pollfds;
	assert(i >= 0 && i < max_pollfd);
	memmove(pollfds + i, pollfds + i + 1, 
		(max_pollfd - i - 1) * sizeof(struct pollfd));
	memmove(pollcbs + i, pollcbs + i + 1, 
		(max_pollfd - i - 1) * sizeof(struct pollcb));
	max_pollfd--;
}

static void poll_callbacks(int n)
{
	int k;

	for (k = 0; k < max_pollfd && n > 0; k++) {
		struct pollfd *f = pollfds + k;
		if (f->revents) { 
			struct pollcb *c = pollcbs + k;
			c->cb(f, c->data);
			n--;
		}
	}
}

/* Run signal handler only directly after event loop */
int event_signal(int sig)
{
	static int first = 1;
	sigset_t mask;
	if (first && sigprocmask(SIG_BLOCK, NULL, &event_sigs) < 0) 
		return -1;
	first = 0;
	if (sigprocmask(SIG_BLOCK, NULL, &mask) < 0)
		return -1;
	sigaddset(&mask, sig);
	if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
		return -1;
	return 0;
}

/* Handle old glibc without ppoll. */
static int ppoll_fallback(struct pollfd *pfd, nfds_t nfds, 
			  const struct timespec *ts, const sigset_t *ss)
{
	sigset_t origmask;
	int ready;
	sigprocmask(SIG_SETMASK, ss, &origmask);
	ready = poll(pfd, nfds, ts ? ts->tv_sec : -1);
	sigprocmask(SIG_SETMASK, &origmask, NULL);
	return ready;
}

static int (*ppoll_vec)(struct pollfd *, nfds_t, const struct timespec
			*, const sigset_t *);

void eventloop(void)
{
#if __GLIBC__ == 2 && __GLIBC_MINOR__ >= 5 || __GLIBC__ > 2
	ppoll_vec = ppoll;
#endif
	if (!ppoll_vec) 
		ppoll_vec = ppoll_fallback;

	for (;;) { 
		int n = ppoll_vec(pollfds, max_pollfd, NULL, &event_sigs);
		if (n <= 0) {
			if (n < 0 && errno != EINTR)
				SYSERRprintf("poll error");
			continue;
		}
		poll_callbacks(n); 
	}			
}
