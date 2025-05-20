/* detect
   linux commit 6dfbbae14a7b961f41d80a106e1ab60e86d061c5
   fs: proc: store PDE()->data into inode->i_private */

#include <linux/proc_fs.h>

void* __something(const struct inode* i);

void* __something(const struct inode* i)
{
        return pde_data (i);
}
