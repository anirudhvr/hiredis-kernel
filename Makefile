#
# Makefile for hiredis kernel client
#
#

obj-m    += testredismod.o 

#
# FIXME: change the following to point to your kernel build folder
#
all: 
	make -C /home/avr/linux-2.6.22.14 M=$(PWD) modules

clean: 
	make -C /home/avr/linux-2.6.22.14 M=$(PWD) clean
	rm -rf *~

testredismod-objs := sds.o redisclient.o networking_utils.o testredis.o
