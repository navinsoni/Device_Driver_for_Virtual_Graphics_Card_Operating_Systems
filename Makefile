obj-m += mymod.o

default:
	$(MAKE) -C /usr/src/linux SUBDIRS=$(PWD) modules

clean:
	rm *.ko
	rm *.o
	rm *.mod.c
