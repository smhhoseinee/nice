obj-m += tcp_nice.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

.PHONY: all clean test server

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	gcc -o test test.c -Wall
	gcc -o server server.c -Wall

clean:
	rm -f *.o *.ko *.mod.c Module.symvers modules.order .*.cmd *.mod test server
	rm -rf .tmp_versions

test: test.c
	gcc -o test test.c -Wall

server: server.c
	gcc -o server server.c -Wall

install:
	sudo insmod tcp_nice.ko
	lsmod | grep tcp_nice

remove:
	sudo rmmod tcp_nice