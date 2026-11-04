#include <stdlib.h>
#include <stdarg.h>

int xasprintf(char **strp, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int xvasprintf(char **ret, const char *format, va_list ap) __attribute__((format(printf, 2, 0)));
void *xalloc(size_t size);
void *xalloc_nonzero(size_t size);
void *xrealloc(void *old, size_t size);
char *xstrdup(char *str);
void Enomem(void);
