#if defined(CONFIG_ARCH_SUPPORTS_UPROBES) && defined(CONFIG_UPROBES)
#include <linux/wait.h>
#include <linux/uprobes.h>

// check for linux commit da09a9e0c3eab1
int handler(struct uprobe_consumer *self, struct pt_regs *regs, __u64 *data);
int handler(struct uprobe_consumer *self, struct pt_regs *regs, __u64 *data) { return 0; }

struct uprobe_consumer* foo(void);
struct uprobe_consumer* foo(void) {
  static struct uprobe_consumer uc;
  uc.handler = handler;
  return &uc;
}


#endif

