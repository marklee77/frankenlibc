BUILDDIR?=../build
STAGEDIR?=$(BUILDDIR)/stage
LIBDIR=$(STAGEDIR)/lib
LIB=$(LIBDIR)/libplatform.a
OBJDIR=$(BUILDDIR)/platform
RANLIB?=ranlib
PLATFORMS=freebsd linux netbsd qemu-arm spike

.PHONY:		$(PLATFORMS) deterministic clean test

default:	linux

$(OBJDIR):
		mkdir -p $@

deterministic:
		$(MAKE) -C $@ clean
		$(MAKE) -C $@
		$(AR) cr $(LIB) $(OBJDIR)/*.o
		$(RANLIB) $(LIB)

$(PLATFORMS):	$(OBJDIR)
		$(MAKE) -C $@
		$(AR) cr $(LIB) $(OBJDIR)/*.o
		$(RANLIB) $(LIB)

clean:		
		for dir in $(PLATFORMS) deterministic; \
		do \
			$(MAKE) -C $$dir clean; \
		done
		rm -f $(LIB)
