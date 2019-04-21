obj-m += m.o
KERNEL := /usr/src/linux-headers-4.14.98+/
all:
	make -C $(KERNEL) M=$(PWD) modules
clean:
	make -C $(KERNEL) M=$(PWD) clean