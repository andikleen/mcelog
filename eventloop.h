#include <poll.h>

typedef void (*poll_cb_t)(int fd, int revents, void *data);

int register_pollcb(int fd, int events, poll_cb_t cb, void *data);
void unregister_pollcb(int fd);
void eventloop(void);
