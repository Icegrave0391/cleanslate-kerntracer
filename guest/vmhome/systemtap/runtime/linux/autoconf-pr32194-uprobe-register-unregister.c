#include <linux/uprobes.h>

void* c = & uprobe_unregister_nosync;
void* d = & uprobe_unregister_sync;
