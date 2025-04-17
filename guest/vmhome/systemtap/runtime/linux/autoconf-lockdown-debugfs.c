#include <linux/security.h>

int foo(void);

int foo(void) {
        return security_locked_down(LOCKDOWN_DEBUGFS);
}
