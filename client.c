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
