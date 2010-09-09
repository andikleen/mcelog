/* Copyright (C) 2009 Intel Corporation 
   Author: Andi Kleen
   Manage trigger commands running as separate processes.

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
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include "trigger.h"
#include "eventloop.h"
#include "list.h"
#include "mcelog.h"
#include "memutil.h"
#include "config.h"

struct child {
	struct list_head nd;
	pid_t child;
	const char *name;
};

static LIST_HEAD(childlist);
static int num_children;
static int children_max = 4;
static char *trigger_dir;

pid_t mcelog_fork(const char *name)
{
	pid_t child;
	struct child *c;

	child = fork();
	if (child <= 0)
		return child;

	num_children++;
	c = xalloc(sizeof(struct child));
	c->name = name;
	c->child = child;
	list_add_tail(&c->nd, &childlist);
	return child;
}

// note: trigger must be allocated, e.g. from config
void run_trigger(char *trigger, char *argv[], char **env)
{
	pid_t child;
	char *fallback_argv[] = {
		trigger,
		NULL,
	};

	if (!argv) 
		argv = fallback_argv;

	Lprintf("Running trigger `%s'\n", trigger);	
	if (children_max > 0 && num_children >= children_max) { 
		Eprintf("Too many trigger children running already\n");
		return;
	}

	child = mcelog_fork(trigger);
	if (child < 0) { 
		SYSERRprintf("Cannot create process for trigger");
		return;
	}
	if (child == 0) { 
		if (trigger_dir)
			chdir(trigger_dir);
		execve(trigger, argv, env);	
		_exit(127);	
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
	if (waitpid(si->si_pid, &status, WNOHANG) < 0) {
		SYSERRprintf("Cannot collect child %d", si->si_pid);
		return;
	}
	finish_child(si->si_pid, status);
}
 
void trigger_setup(void)
{
	char *s;
	struct sigaction sa = {
		.sa_sigaction = child_handler,
		.sa_flags = SA_SIGINFO|SA_NOCLDSTOP|SA_RESTART,
	};
	sigaction(SIGCHLD, &sa, NULL);
	event_signal(SIGCHLD);

	config_number("trigger", "children-max", "%d", &children_max);

	s = config_string("trigger", "directory");
	if (s) { 
		if (access(s, R_OK|X_OK) < 0) 
			SYSERRprintf("Cannot access trigger directory `%s'", s);
		trigger_dir = s;
	}
}

void trigger_wait(void)
{
	int sig;
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	while (num_children > 0) {
		if (sigwait(&mask, &sig) < 0)
			SYSERRprintf("sigwait waiting for children");
	}
}

int trigger_check(char *s)
{
	char *name;
	int rc;

	if (trigger_dir)
		asprintf(&name, "%s/%s", trigger_dir, s);
	else
		name = s;

	rc = access(name, R_OK|X_OK);

	if (trigger_dir)
		free(name);

	return rc;
}
