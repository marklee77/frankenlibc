COMPILE.c?=$(CC) -c $(CPPFLAGS) $(CFLAGS)
COMPILE.S?=$(CC) -c $(CPPFLAGS) $(ASFLAGS)
BUILDDIR?=../../build
OBJDIR=$(BUILDDIR)/franken
OBJ=$(OBJDIR)/posix-host.o $(OBJDIR)/virtio_net.o
STAGEDIR?=$(BUILDDIR)/stage
INCLUDES=-I../include -I../init -I../thread -I../ucontext -I../../platform/include -I. -I$(STAGEDIR)/include
LKLSRC?=../../../lkl-linux
LKL_INCLUDES=-I$(LKLSRC)/include/uapi -I$(LKLSRC)/include
LKL_INCLUDES+=-I$(LKLSRC)/arch/lkl/include/ -I$(LKLSRC)/arch/lkl/include/uapi
LKL_INCLUDES+=-I$(LKLSRC)/arch/lkl/include/generated -I$(LKLSRC)/arch/lkl/include/generated/uapi
INCLUDES+=$(LKL_INCLUDES)
CPPFLAGS+=-nostdinc $(INCLUDES)
CFLAGS+=-fno-stack-protector

default:			$(OBJ)

$(OBJDIR)/posix-host.o:		posix-host.c 
				@mkdir -p $(OBJDIR)
				$(COMPILE.c) -o $@ posix-host.c

$(OBJDIR)/virtio_net.o:		virtio_net.c 
				@mkdir -p $(OBJDIR)
				$(COMPILE.c) -o $@ virtio_net.c

.PHONY:				clean
clean:		
				rm -f $(OBJ)
