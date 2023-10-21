obj-m += isr.o

isr-objs := interrupt_demo.o 
 
KDIR = /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR)  M=$(shell pwd) modules
	gcc -o app app.c
clean:
	make -C $(KDIR)  M=$(shell pwd) clean
