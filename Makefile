KDIR := /lib/modules/$(shell uname -r)/build

ccflags-y := -std=gnu99 -Wno-declaration-after-statement
obj-m     += kqueue.o

all:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	rm -rf *.o *.ko *.mod.* *.cmd .module* modules* Module* .*.cmd .tmp*
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
