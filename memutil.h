#include <stdlib.h>

int xvasprintf(char **ret, const char *format, va_list ap);
void *xalloc(size_t size);
void *xalloc_nonzero(size_t size);
void *xrealloc(void *old, size_t size);
char *xstrdup(char *str);
void Enomem(void);
