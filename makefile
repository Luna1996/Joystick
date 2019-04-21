obj-m += m.o

all:
	make -C /lib/modules/$(shell uname -r)/kernel M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/kernel M=$(PWD) clean