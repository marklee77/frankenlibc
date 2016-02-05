DIRS=libc lwp stubs thread init ucontext malloc lkl
BUILDDIR?=../build
STAGEDIR?=$(BUILDDIR)/stage
LIBDIR=$(STAGEDIR)/lib
LIB=$(LIBDIR)/libfranken.a
OBJDIR=$(BUILDDIR)/franken
RANLIB?=ranlib

.PHONY:		$(DIRS) clean test

default:	$(DIRS) $(LIB)

$(DIRS):	
		$(MAKE) -C $@

$(LIB):		$(DIRS)
		mkdir -p $(LIBDIR)
		$(AR) cr $@ $(OBJDIR)/*.o
		$(RANLIB) $@

test:		default
		$(MAKE) -C $@

clean:		
		for dir in $(DIRS) libc test; \
		do \
			$(MAKE) -C $$dir clean; \
		done
		rm -f $(LIB)
