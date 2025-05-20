#include <stdio.h>

struct foo {
unsigned a: 4;
unsigned b: 8;
char c[100];
int d: 3;
};

struct foo f;
void main() {
  struct foo g;
  f.d = 4;
  f.a = 3;
  g.b = 2;
  g.d = 1;
 f3:
  printf("%d %u %u %d\n", f.d, f.a, g.b, g.d);
  fflush(stdout);
 f4:
  printf("%d %u %u %d\n", f.d, f.a, g.b, g.d);  
  fflush(stdout);
  if (0) goto f3;
  if (0) goto f4;
}


