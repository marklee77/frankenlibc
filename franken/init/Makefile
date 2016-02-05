COMPILE.c?=$(CC) -c $(CPPFLAGS) $(CFLAGS)
COMPILE.S?=$(CC) -c $(CPPFLAGS) $(ASFLAGS)
BUILDDIR?=../../build
OBJDIR=$(BUILDDIR)/franken
OBJ=$(OBJDIR)/fdinit.o
OBJ+=$(OBJDIR)/start_main.o
STAGEDIR?=$(BUILDDIR)/stage
INCLUDES=-I../include -I../thread -I../ucontext -I../../platform/include -I. -I$(STAGEDIR)/include
LKLSRC?=../../../lkl-linux
LKL_INCLUDES=-I$(LKLSRC)/include/uapi -I$(LKLSRC)/include
LKL_INCLUDES+=-I$(LKLSRC)/arch/lkl/include/ -I$(LKLSRC)/arch/lkl/include/uapi
LKL_INCLUDES+=-I$(LKLSRC)/arch/lkl/include/generated -I$(LKLSRC)/arch/lkl/include/generated/uapi
INCLUDES+=$(LKL_INCLUDES)
CPPFLAGS+=-nostdinc $(INCLUDES)
CFLAGS+=-fno-stack-protector
DEPS=init.h

default:			$(OBJ)

$(OBJDIR)/fdinit.o:		fdinit.c $(DEPS)
				@mkdir -p $(OBJDIR)
				$(COMPILE.c) -o $@ fdinit.c

$(OBJDIR)/start_main.o:		start_main.c $(DEPS)
				@mkdir -p $(OBJDIR)
				$(COMPILE.c) -o $@ start_main.c

.PHONY:				clean
clean:		
				rm -f $(OBJ)
