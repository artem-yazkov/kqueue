TARGET_MODULE := kqueue

KDIR := /lib/modules/$(shell uname -r)/build

ccflags-y := -std=gnu99 -Wall -Wno-declaration-after-statement

$(TARGET_MODULE)-objs := kqueue-queue.o kqueue-chrdev.o kqueue-file.o
obj-m                 := $(TARGET_MODULE).o

all:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
	$(MAKE) kqueue-popd.c ./kqueue-popd
	$(MAKE) kqueue-push.c ./kqueue-push

clean:
	rm -rf *.o *.ko *.mod.* *.cmd .module* modules* Module* .*.cmd .tmp*
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
