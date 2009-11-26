#include <poll.h>

typedef void (*poll_cb_t)(struct pollfd *pfd, void *data);

int register_pollcb(int fd, int events, poll_cb_t cb, void *data);
void unregister_pollcb(struct pollfd *pfd);
void eventloop(void);
int event_signal(int sig);
