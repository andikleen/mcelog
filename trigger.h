void run_trigger(char *trigger, char *argv[], char **env);
void trigger_setup(void);
void trigger_wait(void);
int trigger_check(char *);
pid_t mcelog_fork(const char *thread_name);
