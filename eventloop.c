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
#include <assert.h>
#include <errno.h>
#include <string.h>
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

int register_pollcb(int fd, int events, poll_cb_t cb, void *data)
{
	int i = max_pollfd;

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
void unregister_pollcb(int fd)
{
	int i;

	for (i = 0; i < max_pollfd; i++) 
		if (pollfds[i].fd == fd)
			break;
	assert(i < max_pollfd);
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
			c->cb(f->fd, f->revents, c->data);
			n--;
		}
	}
}

void eventloop(void)
{
	for (;;) { 
		int n = poll(pollfds, max_pollfd, -1);
		if (n <= 0) {
			if (n < 0)
				SYSERRprintf("poll error");
			continue;
		}
		poll_callbacks(n); 
	}			
}
