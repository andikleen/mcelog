/* Copyright (C) 2009 Intel Corporation
   Author: Andi Kleen
   Client code to talk to the mcelog server.

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
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "mcelog.h"
#include "client.h"
#include "paths.h"
#include "config.h"

/* Send a command to the mcelog server and dump output */
void ask_server(char *command)
{
	struct sockaddr_un sun;
	int fd;
	int n;
	char buf[1024];
	int done;
	char *path = config_string("server", "socket-path");
	if (!path)
		path = SOCKET_PATH;

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		SYSERRprintf("client socket");

	sun.sun_family = AF_UNIX;
	sun.sun_path[sizeof(sun.sun_path)-1] = 0;
	strncpy(sun.sun_path, path, sizeof(sun.sun_path)-1);

	if (connect(fd, (struct sockaddr *)&sun,
			sizeof(struct sockaddr_un)) < 0)
		SYSERRprintf("client connect");

	n = strlen(command);
	if (write(fd, command, n) != n)
		SYSERRprintf("client command write");

	done = 0;
	while (!done && (n = read(fd, buf, sizeof buf)) > 0) {
		if (n >= 5 && !memcmp(buf + n - 5, "done\n", 5)) {
			n -= 5;
			done = 1;
		}
		write(1, buf, n);
	}
	if (n < 0)
		SYSERRprintf("client read");
}
