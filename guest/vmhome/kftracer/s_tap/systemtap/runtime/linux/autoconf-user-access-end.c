#include <linux/uaccess.h>
void foo(void);
void foo(void)
{
	user_access_end();
}
