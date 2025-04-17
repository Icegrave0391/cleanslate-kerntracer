#include <stdio.h>

int __attribute__ ((noinline))
foo(int a, int b)
{
	return 0;
}

int __attribute__ ((noinline))
bar(int a, int b)
{
	return b;
}

int main(int argc, char *argv[])
{
	printf("foo(1,2) = %d\n", foo(1,2));
	printf("bar(1,2) = %d\n", bar(1,2));
	return 0;
}
