intelli2-objs:=test_main.o intel_netlink.o
obj-m:=intelli2.o
all:
	make -C /usr/src/linux-headers-4.2.0-27-generic M=`pwd` modules
clean:
	@-rm  -rf *.o *.ko *.mod.c *.order Module.symvers .tmp_versions .*.cmd
