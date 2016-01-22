intel-objs:=test_main.o intel_netlink.o
obj-m:=intel.o
all:
	make -C /usr/src/kernels/3.18.7-100.fc20.x86_64 M=`pwd` modules
clean:
	@-rm  -rf *.o *.ko *.mod.c *.order Module.symvers .tmp_versions .*.cmd
