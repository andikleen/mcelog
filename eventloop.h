#include <poll.h>

int register_pollcb(int fd, int events, 
		    void (*cb)(struct pollfd *, void *), void *data);
void unregister_pollcb(struct pollfd *);
void eventloop(void);
