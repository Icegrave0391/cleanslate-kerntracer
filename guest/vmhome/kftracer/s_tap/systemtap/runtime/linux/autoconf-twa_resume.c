#include <linux/task_work.h>

// Check to see if there is a define or enum for TWA_RESUME.
// Older kernels just pass in true or false into task_work_add()
int twa_resume_exist(void);

int twa_resume_exist(void) {
  return (TWA_RESUME == TWA_RESUME);
}
