/* Copyright (C) 20014 Intel Corporation
   Author: Rui Wang
   Handle 'Bus and Interconnect' error threshold indications.

   mcelog is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; version
   2.

   mcelog is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should find a copy of v2 of the GNU General Public License somewhere
   on your Linux system. */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "memutil.h"
#include "mcelog.h"
#include "config.h"
#include "trigger.h"
#include "bus.h"

static char *bus_trigger, *iomca_trigger;

enum {
	MAX_ENV = 20,
};

void bus_setup(void)
{
	bus_trigger = config_string("socket", "bus-uc-threshold-trigger");
	if (bus_trigger && trigger_check(bus_trigger) < 0) {
		SYSERRprintf("Cannot access bus threshold trigger `%s'",
				bus_trigger);
		exit(1);
	}

	iomca_trigger = config_string("socket", "iomca-threshold-trigger");
	if (iomca_trigger && trigger_check(iomca_trigger) < 0) {
		SYSERRprintf("Cannot access iomca threshold trigger `%s'",
				iomca_trigger);
		exit(1);
	}
}

void run_bus_trigger(int socket, int cpu, char *level, char *pp, char *rrrr,
		char *ii, char *timeout)
{
	int ei = 0;
	char *env[MAX_ENV];
	int i;
	char *msg;
	char *location;

	if (!bus_trigger)
		return;

	if (socket >= 0)
		asprintf(&location, "CPU %d on socket %d", cpu, socket);
	else
		asprintf(&location, "CPU %d", cpu);
	asprintf(&msg, "%s received Bus and Interconnect Errors in %s",
		location, ii);
	asprintf(&env[ei++], "LOCATION=%s", location);
	free(location);

	if (socket >= 0)
		asprintf(&env[ei++], "SOCKETID=%d", socket);
	asprintf(&env[ei++], "MESSAGE=%s", msg);
	asprintf(&env[ei++], "CPU=%d", cpu);
	asprintf(&env[ei++], "LEVEL=%s", level);
	asprintf(&env[ei++], "PARTICIPATION=%s", pp);
	asprintf(&env[ei++], "REQUEST=%s", rrrr);
	asprintf(&env[ei++], "ORIGIN=%s", ii);
	asprintf(&env[ei++], "TIMEOUT=%s", timeout);
	env[ei] = NULL;
	assert(ei < MAX_ENV);

	run_trigger(bus_trigger, NULL, env);
	for (i = 0; i < ei; i++)
		free(env[i]);
	free(msg);
}

void run_iomca_trigger(int socket, int cpu, int seg, int bus, int dev, int fn)
{
	int ei = 0;
	char *env[MAX_ENV];
	int i;
	char *msg;
	char *location;

	if (!iomca_trigger)
		return;

	if (socket >= 0)
		asprintf(&location, "CPU %d on socket %d", cpu, socket);
	else
		asprintf(&location, "CPU %d", cpu);
	asprintf(&msg, "%s received IO MCA Errors from %x:%02x:%02x.%x",
		location, seg, bus, dev, fn);
	asprintf(&env[ei++], "LOCATION=%s", location);
	free(location);

	if (socket >= 0)
		asprintf(&env[ei++], "SOCKETID=%d", socket);
	asprintf(&env[ei++], "MESSAGE=%s", msg);
	asprintf(&env[ei++], "CPU=%d", cpu);
	asprintf(&env[ei++], "SEG=%x", seg);
	asprintf(&env[ei++], "BUS=%02x", bus);
	asprintf(&env[ei++], "DEVICE=%02x", dev);
	asprintf(&env[ei++], "FUNCTION=%x", fn);
	env[ei] = NULL;
	assert(ei < MAX_ENV);

	run_trigger(iomca_trigger, NULL, env);
	for (i = 0; i < ei; i++)
		free(env[i]);
	free(msg);

}
