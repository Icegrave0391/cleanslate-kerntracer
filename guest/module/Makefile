# obj-m += deeplog.o
# obj-m += vmcall_test.o
# obj-m += vmcall_test2.o
obj-m += ept_test.o
ept_test-objs := ept_pte_test.o codepages.o

PWD := $(CURDIR)
KVERN := $(shell uname -r)

all:
	        make CC=clang -C /lib/modules/$(KVERN)/build M=$(PWD) modules
			rm -rf *.mod* .*.cmd *.symvers *.order *.o
clean:
	        make CC=clang -C /lib/modules/$(KVERN)/build M=$(PWD) clean
