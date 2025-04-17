#include <linux/nsproxy.h>

long foo(void);
long foo(void) {
    struct nsset set = {};
    return (long) &set.nsproxy;
}
