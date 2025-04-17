int foobar(){
    return 0;
}

int foo1(){
    return 1;
}

// foo2 will be in the symbol table but not the debug sections
asm(".type   foo2, @function \n\
     foo2:           \n\
     ret             \n\
");
extern void foo2(void);

void main(){
    return;
}