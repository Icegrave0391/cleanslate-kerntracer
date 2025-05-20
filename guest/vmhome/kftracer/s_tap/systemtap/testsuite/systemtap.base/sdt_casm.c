#include <sys/sdt.h>

int main()
{
    int x = 42;
    int y = 43;
    __asm__ __volatile__ (
            STAP_PROBE_ASM(testsuite, probe0, STAP_PROBE_ASM_TEMPLATE(0))
            );
    __asm__ __volatile__ (
            STAP_PROBE_ASM(testsuite, probe1, STAP_PROBE_ASM_TEMPLATE(1))
            :: STAP_PROBE_ASM_OPERANDS(1, x)
            );
    // Create a template to test explicit template support
    __asm__ __volatile__ (
			  STAP_PROBE_ASM(testsuite, probe2, -4@%[ARG1] -4@%[ARG2])
			  :: [ARG1] "rm" (x), [ARG2] "rm" (y));
    return 0;
}
