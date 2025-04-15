#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <linux/skbuff.h>

#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>

#include <linux/seq_file.h>

MODULE_LICENSE("GPL");

#define NUMBER_OF_SYSCALLS 336
#define NUMBER_OF_SUBSYSTEMS 11
#define NUMBER_OF_FUNCTIONS 5000

extern int snprintf(char *buf, size_t size, const char *fmt, ...);

extern void deeplog_start_logging(void);
extern void deeplog_stop_logging(void);
extern int subsystem_call_count[22];
extern uint64_t sc_total_time;
extern unsigned long number_of_syscall_calls;
extern int DEEPLOG_IS_CF_LOGGING;

extern int numOfFunctionsInSyscall[NUMBER_OF_SYSCALLS];
extern int functionsInSyscall[NUMBER_OF_SYSCALLS][NUMBER_OF_FUNCTIONS];

extern int get_function_in_syscall(int syscall, int function);

int init_module(void)
{
    pr_info("starting control flow profiling\n");
    pr_info("size: %x\n", sizeof(struct sk_buff));
    deeplog_start_logging();
  DEEPLOG_IS_CF_LOGGING = 1;
    return 0;
}

void dumpIntoFile(struct file *file, int data[NUMBER_OF_SYSCALLS][NUMBER_OF_FUNCTIONS], int size[NUMBER_OF_SYSCALLS]) {
    loff_t pos = 0;
    int bufLen;
    char buf[7];
    int total = 0;

    for (int i = 0; i < NUMBER_OF_SYSCALLS; i++) {
        pos += kernel_write(file, "{", 1, &pos);
        if (size[i] > 0) {
          pr_err("Syscall %d: %d", i, size[i]);
          total += size[i];
        }
        for (int k = 0; k < 7; k++) {
          buf[k] = '\0';
        }
        for (int j = 0; j < size[i]; j++) {
            bufLen = snprintf(&buf[0], 7, "%u", get_function_in_syscall(i, j));
            pos += kernel_write(file, buf,  bufLen, &pos);
            pos += kernel_write(file, ", ", 2, &pos);
        }
        pos += kernel_write(file, "}\n", 2, &pos);
    }
    pr_err("total functions profiled: %d", total);
}

void cleanup_module(void)
{
    struct file *file;

    file = filp_open("/home/seclog/functionsInSyscallRaw", O_CREAT | O_RDWR, 777);
    dumpIntoFile(file, functionsInSyscall, numOfFunctionsInSyscall);
    filp_close(file, 0);

  DEEPLOG_IS_CF_LOGGING = 0;
  deeplog_stop_logging();
    pr_info("Completed Control Flow Profiling!\n");
}

