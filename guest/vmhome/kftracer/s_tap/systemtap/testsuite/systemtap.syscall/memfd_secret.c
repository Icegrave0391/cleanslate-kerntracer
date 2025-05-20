/* COVERAGE: memfd_secret */

/*
 * Glibc doesn't support memfd_secret yet, so we have to use syscall(2)
 */
#define _GNU_SOURCE
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>

int main()
{
#ifdef __NR_memfd_secret
    int fd;
    fd = syscall(__NR_memfd_secret, O_CLOEXEC);
    //staptest// memfd_secret (O_CLOEXEC) = NNNN

    close(fd);
#endif
    
    return 0;
}

