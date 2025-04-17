#include <linux/mm.h>
#include <asm/tlbflush.h>

/* this function was no longer an inline since upstream kernel commit
 * af5c40c6ee */
bool foo(void);

bool foo(void)
{
    return nmi_uaccess_okay();
}
