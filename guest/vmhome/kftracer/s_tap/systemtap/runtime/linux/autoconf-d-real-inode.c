#include <linux/fs.h>

/* kernel function d_real_inode() is missing in older kernels like
 * kernel-3.10.0-327.el7.x86_64 */
void foo(void);
void foo(void) {
	struct inode *ino = d_real_inode(NULL);
	(void) ino;
}
