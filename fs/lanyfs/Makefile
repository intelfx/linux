#
# Makefile for the linux lanyard filesystem routines.
#

obj-$(CONFIG_LANYFS_FS) += lanyfs.o

lanyfs-y := msg.o misc.o icache.o inode.o \
	    btree.o dir.o extender.o file.o \
	    chain.o super.o lanyfs_km.o

ccflags-$(CONFIG_LANYFS_DEBUG) += -DLANYFS_DEBUG

