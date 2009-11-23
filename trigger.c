/* Manage trigger commands */
#define _GNU_SOURCE 1
#include <spawn.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include "trigger.h"
#include "list.h"
#include "mcelog.h"
#include "memutil.h"
#include "config.h"

struct child {
	struct list_head nd;
	pid_t child;
	char *name;
};

static LIST_HEAD(childlist);
static int num_children;
static int children_max = 4;

// note: trigger must be allocated, e.g. from config
void run_trigger(char *trigger, char *argv[], char **env)
{
	int err;
	struct child *c;
	char *fallback_argv[] = {
		trigger,
		NULL,
	};

	if (!argv) 
		argv = fallback_argv;

	Lprintf("Running trigger `%s'", trigger);	
	if (children_max > 0 && num_children >= children_max) { 
		Eprintf("Too many trigger children running already\n");
		return;
	}
	num_children++;

	c = xalloc(sizeof(struct child));
	c->name = trigger;
	list_add_tail(&c->nd, &childlist);

	// XXX split trigger into argv?

	err = posix_spawnp(&c->child, trigger, NULL, NULL, argv, env);
	if (err) { 
		SYSERRprintf("Cannot spawn trigger `%s'", trigger);
		list_del(&c->nd);
		free(c);
		return;
	}
}

/* Clean up child on SIGCHLD */
static void finish_child(pid_t child, int status)
{
	struct child *c, *tmpc;

	list_for_each_entry_safe (c, tmpc, &childlist, nd) {
		if (c->child == child) { 
			if (WIFEXITED(status) && WEXITSTATUS(status)) { 
				Eprintf("Trigger `%s' exited with status %d\n",
					c->name, WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) { 
				Eprintf("Trigger `%s' died with signal %s\n",
					c->name, strsignal(WTERMSIG(status)));
			}
			list_del(&c->nd);
			free(c);		
			num_children--;
			return;
		}
	}
	abort();
}

/* Runs only directly after ppoll */
static void child_handler(int sig, siginfo_t *si, void *ctx)
{
	int status;
	if (waitpid(si->si_pid, &status, WNOHANG) < 0) 
		SYSERRprintf("Cannot collect children %d", si->si_pid);
	finish_child(si->si_pid, status);
}
 
void trigger_setup(void)
{
	struct sigaction sa = {
		.sa_sigaction = child_handler,
		.sa_flags = SA_SIGINFO|SA_NOCLDSTOP|SA_RESTART,
	};
	config_number("trigger", "children-max", "%d", &children_max);
	sigaction(SIGCHLD, &sa, NULL);
}
