obj-m += cfprofile.o

PWD := $(CURDIR)

all:
	            make CC=clang -C /lib/modules/6.1.9/build M=$(PWD) modules
clean:
	            make CC=clang -C /lib/modules/6.1.9/build M=$(PWD) clean
