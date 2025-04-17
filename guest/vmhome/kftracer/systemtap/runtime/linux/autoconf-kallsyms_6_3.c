#include <linux/module.h>

int module_kallsyms_on_each_symbol(const char *modname,
				   int (*fn)(void *, const char *, struct module*,
					     unsigned long),
				   void *data);
