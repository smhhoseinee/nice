# Kernel module plus the two user-space test programs.
obj-m += tcp_nice.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

.PHONY: all module tools clean load unload

all: module tools

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

tools: test server

test: test.c
	gcc -Wall -o test test.c

server: server.c
	gcc -Wall -o server server.c

load: module
	sudo insmod tcp_nice.ko
	lsmod | grep tcp_nice

unload:
	sudo rmmod tcp_nice

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f test server
