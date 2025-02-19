ifneq ($(KERNELRELEASE),)
# call from kernel build system

# scull-objs := main.o pipe.o access.o

obj-m	:= scdd.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif



clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions modules.* Module.*