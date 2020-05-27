#ifndef __TRIGGER_H__
#define __TRIGGER_H__

#include <stdbool.h>
void run_trigger(char *trigger, char *argv[], char **env, bool sync, const char* reporter);
void trigger_setup(void);
void trigger_wait(void);
int trigger_check(char *);
pid_t mcelog_fork(const char *thread_name);

#endif
