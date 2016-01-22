obj-m:=test_main.o
all:
	make -C /usr/src/kernels/3.18.7-100.fc20.x86_64 M=`pwd` modules
clean:
	@-rm *.o *.ko *.mod.c *.order Module.symvers
