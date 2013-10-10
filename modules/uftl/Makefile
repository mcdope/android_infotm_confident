obj-m += uftl.o

uftl-y += uftl_dev.o uftl_core.o uftl_io.o uftl_cache_stat.o

RMMODS := *.o *.ko *.~ *.mod.c *.order .*.cmd *.symvers
clean:
	rm -rf $(RMMODS)

