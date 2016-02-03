COMPILE.c?=$(CC) -c $(CPPFLAGS) $(CFLAGS)
COMPILE.S?=$(CC) -c $(CPPFLAGS) $(ASFLAGS)
RUMPOBJ?=../../rumpobj
OBJDIR=$(RUMPOBJ)/franken
OBJ=$(OBJDIR)/posix-host.o
#OBJ=$(OBJDIR)/stdio-init.o
RUMP?=$(RUMPOBJ)/rump
INCLUDES=-I../include -I../thread -I../ucontext -I../../platform/include -I. -I$(RUMP)/include
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

$(OBJDIR)/stdio-init.o:		stdio-init.c 
				@mkdir -p $(OBJDIR)
				$(COMPILE.c) -o $@ stdio-init.c

.PHONY:				clean
clean:		
				rm -f $(OBJ)
