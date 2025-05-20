#include <linux/kallsyms.h>
int kallsyms_on_each_symbol(int (*fn)(void *, const char *, unsigned long),
			    void *data);
