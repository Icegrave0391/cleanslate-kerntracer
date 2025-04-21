/* 
 * Make sure linux/genhd.h exists and is usable.
 *
 * Some kernels (RHEL9) have removed genhd.h via backporting
 * and just using the kernel version is not sufficent.
 */
#include <linux/genhd.h>
