obj-m += m.o

all:
	make -C /lib/modules/$(shell uname -r)/modules M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/modules M=$(PWD) clean