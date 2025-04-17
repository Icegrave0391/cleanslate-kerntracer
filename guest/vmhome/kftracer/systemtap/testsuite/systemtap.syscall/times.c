/* COVERAGE: times */

#include <sys/times.h>

int main()
{
    struct tms tms;

    times(&tms);
    //staptest// times ({tms_utime=NNNN, tms_stime=NNNN, tms_cutime=NNNN, tms_cstime=NNNN}) = NNNN

// The following SEGVs at the pointer dereference in __times()
#if 0
    times((struct tms *)-1);
#ifdef __s390__
    //staptest// times (0x[7]?[f]+) = -NNNN
#else
    //staptest// times (0x[f]+) = -NNNN
#endif
#endif

    return 0;
}
