/* Copyright (C) 2009 Intel Corporation 
   Author: Andi Kleen
   Simple event-driven unix network server for client access.
   Process commands and buffer output.

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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <signal.h>
#include <setjmp.h>
#include "mcelog.h"
#include "server.h"
#include "eventloop.h"
#include "config.h"
#include "memdb.h"
#include "memutil.h"
#include "paths.h"
#include "page.h"

#define PAIR(x) x, sizeof(x)-1

struct clientcon { 
	char *inbuf;	/* 0 terminated */
	char *inptr;
	char *outbuf;
	size_t outcur;
	size_t outlen;
};

static char *client_path = SOCKET_PATH;
static int initial_ping_timeout = 2;
static struct config_cred acc = { .uid = 0, .gid = -1U };

static void free_outbuf(struct clientcon *cc)
{
	free(cc->outbuf);
	cc->outbuf = NULL;
	cc->outcur = cc->outlen = 0;
}

static void free_inbuf(struct clientcon *cc)
{
	free(cc->inbuf);
	cc->inbuf = NULL;
	cc->inptr = NULL;
}

static void free_cc(struct clientcon *cc)
{
	free(cc->outbuf);
	free(cc->inbuf);
	free(cc);	
}

static void sendstring(int fd, char *str)
{
	(void)send(fd, str, strlen(str), MSG_DONTWAIT|MSG_NOSIGNAL);
}

static void dispatch_dump(FILE *fh, char *s)
{
	char *p;
	enum printflags printflags = 0;

	while ((p = strsep(&s, " ")) != NULL) {
		if (!strcmp(p, "dump"))
			;
		else if (!strcmp(p, "bios"))
			printflags |= DUMP_BIOS;
		else if (!strcmp(p, "all"))
			printflags |= DUMP_ALL;
		else 
			fprintf(fh, "Unknown dump parameter\n");
	}			

	dump_memory_errors(fh, printflags);
	fprintf(fh, "done\n");
}

static void dispatch_pages(FILE *fh)
{
	dump_page_errors(fh);
	fprintf(fh, "done\n");
}

static void dispatch_commands(char *line, FILE *fh)
{
	char *s;
	while ((s = strsep(&line, "\n")) != NULL) { 
		while (isspace(*s))
			line++;
		if (!strncmp(s, "dump", 4))
			dispatch_dump(fh, s);
		else if (!strncmp(s, "pages", 5))
			dispatch_pages(fh);
		else if (!strcmp(s, "ping"))
			fprintf(fh, "pong\n");
		else if (*s != 0)
			fprintf(fh, "Unknown command\n");
	}
}

/* assumes commands don't cross records */
static void process_cmd(struct clientcon *cc)
{
	FILE *fh;

	assert(cc->outbuf == NULL);
	fh = open_memstream(&cc->outbuf, &cc->outlen);
	if (!fh)
		Enomem();
	cc->outcur = 0;
	dispatch_commands(cc->inbuf, fh);
	if (ferror(fh) || fclose(fh) != 0)
		Enomem();
}

/* check if client is allowed to access */
static int access_check(int fd, struct msghdr *msg)
{
	struct cmsghdr *cmsg;	
	struct ucred *uc;

	/* check credentials */
	cmsg = CMSG_FIRSTHDR(msg);
	if (cmsg == NULL || 
		cmsg->cmsg_level != SOL_SOCKET ||
		cmsg->cmsg_type != SCM_CREDENTIALS) { 
		Eprintf("Did not receive credentials over client unix socket %p\n",
			cmsg);
		return -1;
	}
	uc = (struct ucred *)CMSG_DATA(cmsg);
	if (uc->uid == 0 || 
		(acc.uid != -1U && uc->uid == acc.uid) ||
		(acc.gid != -1U && uc->gid == acc.gid))
		return 0;
	Eprintf("rejected client access from pid:%u uid:%u gid:%u\n",
		uc->pid, uc->uid, uc->gid);
	sendstring(fd, "permission denied\n");
	return -1;
}

/* retrieve commands from client */
static int client_input(int fd, struct clientcon *cc)
{
	char ctlbuf[CMSG_SPACE(sizeof(struct ucred))];
	struct iovec miov;
	struct msghdr msg = {
		.msg_iov = &miov,
		.msg_iovlen = 1,
		.msg_control = ctlbuf,
		.msg_controllen = sizeof(ctlbuf),
	}; 	
	int n, n2;

	assert(cc->inbuf == NULL);
	if (ioctl(fd, FIONREAD, &n) < 0)
		return -1;
	if (n == 0)
		return 0;

	cc->inbuf = xalloc_nonzero(n + 1);
	cc->inbuf[n] = 0;
	cc->inptr = cc->inbuf;

	miov.iov_base = cc->inbuf;
	miov.iov_len = n;
	n2 = recvmsg(fd, &msg, 0);
	if (n2 < n)
		return -1;

	return access_check(fd, &msg) == 0 ? n : -1;
}

/* process input/out on client socket */
static void client_event(struct pollfd *pfd, void *data)
{
	int events = pfd->revents;
	struct clientcon *cc = (struct clientcon *)data;
	int n;

	if (events & ~(POLLIN|POLLOUT)) /* error/close */
		goto error;

	if (events & POLLOUT) {
		if (cc->outcur < cc->outlen) {
			n = send(pfd->fd, cc->outbuf + cc->outcur, 
				 cc->outlen - cc->outcur, 
				 MSG_DONTWAIT|MSG_NOSIGNAL);
			if (n < 0) {
				/* EAGAIN here? but should not happen */
				goto error;
			}
			cc->outcur += n;
		}
		if (cc->outcur == cc->outlen)
			free_outbuf(cc);
	}
	if (events & POLLIN) {
		n = client_input(pfd->fd, cc);
		if (n < 0)
			goto error;
		process_cmd(cc);
		free_inbuf(cc);
	}
	pfd->events = cc->outbuf ? POLLOUT : POLLIN;
	return;

error:
	if (pfd->revents & POLLERR)
		SYSERRprintf("error while reading from client");
	close(pfd->fd);
	unregister_pollcb(pfd);
	free_cc(cc);
}

/* accept a new client */
static void client_accept(struct pollfd *pfd, void *data)
{
	struct clientcon *cc = NULL;
	int nfd = accept(pfd->fd, NULL, 0);	
	int on;

	if (nfd < 0) {
		SYSERRprintf("accept failed on client socket");
		return;
	}

	on = 1;
	if (setsockopt(nfd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on)) < 0) {
		SYSERRprintf("Cannot enable credentials passing on client socket");
		goto cleanup;
	}

	cc = xalloc(sizeof(struct clientcon));
	if (register_pollcb(nfd, POLLIN, client_event, cc) < 0) {
		sendstring(nfd, "mcelog server too busy\n");
		goto cleanup;
	}
	return;

cleanup:
	free(cc);
	close(nfd);
}

static void server_config(void)
{
	char *s;
	long v;

	config_cred("server", "client", &acc);
	if ((s = config_string("server", "socket-path")) != NULL)
		client_path = s;
	if (config_number("server", "initial-ping-timeout", "%u", &v) == 0)
		initial_ping_timeout = v;
}

static sigjmp_buf ping_timeout_ctx;

static void ping_timeout(int sig)
{
	siglongjmp(ping_timeout_ctx, 1);
}

/* server still running? */
static int server_ping(struct sockaddr_un *un)
{
	struct sigaction oldsa;
	struct sigaction sa = { .sa_handler = ping_timeout };
	int ret, n;
	char buf[10];
	int fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return 0;

	sigaction(SIGALRM, &sa, &oldsa);
	if (sigsetjmp(ping_timeout_ctx, 1) == 0) {
		ret = -1;
		alarm(initial_ping_timeout);
		if (connect(fd, un, sizeof(struct sockaddr_un)) < 0)
			goto cleanup;
		if (write(fd, PAIR("ping\n")) < 0)
			goto cleanup;
		if ((n = read(fd, buf, 10)) < 0)
			goto cleanup;
		if (n == 5 && !memcmp(buf, "pong\n", 5))
			ret = 0;
	} else
		ret = -1;
cleanup:
	sigaction(SIGALRM, &oldsa, NULL);
	alarm(0);
	close(fd);
	return ret;
}

void server_setup(void)
{
	int fd;
	struct sockaddr_un adr; 
	int on;

	server_config();

	if (client_path[0] == 0)
		return;

	if (strlen(client_path) >= sizeof(adr.sun_path) - 1) {
		Eprintf("Client socket path `%s' too long for unix socket",
				client_path);
		return;
	}

	memset(&adr, 0, sizeof(struct sockaddr_un));
	adr.sun_family = AF_UNIX;
	strncpy(adr.sun_path, client_path, sizeof(adr.sun_path) - 1);

	if (access(client_path, F_OK) == 0) {
		if (server_ping(&adr) == 0) {
			Eprintf("mcelog server already running\n");
			exit(1);
		}
		unlink(client_path);
	}

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		SYSERRprintf("cannot open listening socket");
		return;
	}

	if (bind(fd, (struct sockaddr *)&adr, sizeof(struct sockaddr_un)) < 0) {
		SYSERRprintf("Cannot bind to client unix socket `%s'",
			client_path);
		goto cleanup;
	}


	listen(fd, 10);
	/* Set SO_PASSCRED to avoid race with client connecting too fast */
	/* Ignore error for old kernels */
	on = 1;
	setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));

	register_pollcb(fd, POLLIN, client_accept, NULL);
	return;

cleanup:
	close(fd);
	exit(1);
}


