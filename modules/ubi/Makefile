obj-m += ubi.o

ubi-y += vtbl.o vmt.o upd.o build.o cdev.o kapi.o eba.o io.o wl.o scan.o
ubi-y += misc.o
ubi-y += ubi_transfer.o 

#ubi-y += debug.o
#obj-y += gluebi.o


RMMODS := *.o *.ko *.~ *.mod.c *.order .*.cmd *.symvers
clean:
	rm -rf $(RMMODS)

