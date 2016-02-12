default:
	./build.sh

seccomp:
	./build.sh seccomp

PHONY: clean distclean seccomp

clean:
	rm -rf build/explode build/franken build/platform build/stublibc
	rm -rf build/stage build/tests build/tools
	make clean -C build/platform-musl
	make clean -C build/lkl-musl
	make clean -C build/lkl-linux
	make clean -C build/lkl-linux/tools/lkl

veryclean:
	rm -rf build

distclean: 
	rm -rf build dist
