#include "mcelog.h"
#include "sysfs.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static void domsr(int cpu, int msr, int bit)
{
	char fpath[32];
	unsigned long long data;
	int fd;

	sprintf(fpath, "/dev/cpu/%d/msr", cpu);
	fd = open(fpath, O_RDWR);
	if (fd == -1) {
		switch (errno) {
		case ENOENT:
			SYSERRprintf("Warning: cpu %d offline?, imc_log not set\n", cpu);
			return;
		default:
			SYSERRprintf("Cannot open %s to set imc_log\n", fpath);
			return;
		}
	}
	if (pread(fd, &data, sizeof data, msr) != sizeof data) {
		SYSERRprintf("Cannot read MSR_ERROR_CONTROL from %s\n", fpath);
		goto out;
	}
	data |= bit;
	if (pwrite(fd, &data, sizeof data, msr) != sizeof data) {
		SYSERRprintf("Cannot write MSR_ERROR_CONTROL to %s\n", fpath);
		goto out;
	}
	if (pread(fd, &data, sizeof data, msr) != sizeof data) {
		SYSERRprintf("Cannot re-read MSR_ERROR_CONTROL from %s\n", fpath);
		goto out;
	}
	if ((data & bit) == 0)
		Lprintf("No DIMM detection available on cpu %d (normal in virtual environments)\n", cpu);
out:
	close(fd);
}

static bool in_lockdown(void)
{
	bool ret = false;
	char *lockdown = read_field("/sys/kernel/security", "lockdown");

	if (!lockdown)
		return false;

	if (!strstr(lockdown, "[none]"))
		ret = true;
	free(lockdown);
	lockdown = NULL;
	return ret;
}

/* XXX: assumes all CPUs are already onlined. */
void set_imc_log(int cputype)
{
	int cpu, ncpus = sysconf(_SC_NPROCESSORS_CONF);
	int	msr, bit;

	switch (cputype) {
	case CPU_SANDY_BRIDGE_EP:
	case CPU_IVY_BRIDGE_EPEX:
	case CPU_HASWELL_EPEX:
		msr = 0x17f;	/* MSR_ERROR_CONTROL */
		bit = 0x2;	/* MemError Log Enable */
		break;
	default:
		return;
	}

	if (in_lockdown()) {
		Lprintf("Kernel in lockdown. Cannot enable DIMM error location reporting");
		return;
	}

	for (cpu = 0; cpu < ncpus; cpu++)
		domsr(cpu, msr, bit);
}
