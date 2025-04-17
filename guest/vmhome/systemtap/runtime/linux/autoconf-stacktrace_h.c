#include <asm/stacktrace.h>

// Check to see if there is an <asm/stacktrace.h>
// and a struc stack_frame defined.
// Older kernels have struct stack_frame in <asm/processor.h>.
int stack_frame_size(void);

int stack_frame_size(void) {
  return sizeof(struct stack_frame);
}

