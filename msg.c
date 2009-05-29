#define _GNU_SOURCE 1
#include <stdlib.h>
#include <syslog.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "mcelog.h"

enum syslog_opt syslog_opt = SYSLOG_REMARK;
int syslog_level = LOG_WARNING;

static void opensyslog(void)
{
	static int syslog_opened;
	if (syslog_opened)
		return;
	syslog_opened = 1;
	openlog("mcelog", 0, 0);
}

/* For warning messages that should reach syslog */
void Lprintf(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (syslog_opt & SYSLOG_REMARK) { 
		opensyslog();
		vsyslog(LOG_ERR, fmt, ap);
	} else { 
		vprintf(fmt, ap);
	}
	va_end(ap);
}

/* For errors during operation */
void Eprintf(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (!(syslog_opt & SYSLOG_ERROR)) {
		fputs("mcelog: ", stderr);
		vfprintf(stderr, fmt, ap);
		fputc('\n', stderr);
	} else { 
		opensyslog();
		vsyslog(LOG_ERR, fmt, ap);
	}
	va_end(ap);
}

void SYSERRprintf(char *fmt, ...)
{
	char *err = strerror(errno);
	va_list ap;
	va_start(ap, fmt);
	if (!(syslog_opt & SYSLOG_ERROR)) {
		fputs("mcelog: ", stderr);
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, ": %s\n", err);
	} else { 
		char *fmt2;
		opensyslog();
		asprintf(&fmt2, "%s: %s\n", fmt, err);
		vsyslog(LOG_ERR, fmt2, ap);
		free(fmt2);
	}
	va_end(ap);
}

/* Write to syslog with line buffering */
static int vlinesyslog(char *fmt, va_list ap)
{
	static char line[200];
	int n;
	int lend = strlen(line); 
	int w = vsnprintf(line + lend, sizeof(line)-lend, fmt, ap);
	while (line[n = strcspn(line, "\n")] != 0) {
		line[n] = 0;
		syslog(syslog_level, "%s", line);
		memmove(line, line + n + 1, strlen(line + n + 1) + 1);
	}
	return w;
}

/* For decoded machine check output */
int Wprintf(char *fmt, ...)
{
	int n;
	va_list ap;
	va_start(ap,fmt);
	if (syslog_opt & SYSLOG_ERROR) {
		opensyslog();
		n = vlinesyslog(fmt, ap);
	} else {
		n = vprintf(fmt, ap);
	}
	va_end(ap);
	return n;
}

/* For output that should reach both syslog and normal log */
void Gprintf(char *fmt, ...)
{
	va_list ap;
	va_start(ap,fmt);
	if (syslog_opt & SYSLOG_LOG) {
		vlinesyslog(fmt, ap);
	} else if (syslog_opt & SYSLOG_REMARK) { 
		vprintf(fmt, ap);
		vlinesyslog(fmt, ap);
	} else { 
		vprintf(fmt, ap);
	}
	va_end(ap);
}
