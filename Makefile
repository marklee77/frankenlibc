default:
	./build.sh

seccomp:
	./build.sh seccomp

PHONY: clean distclean seccomp

clean:
	rm -rf build/explode build/franken
	make clean -C build/musl
	make clean -C build/lkl-linux
	make clean -C build/lkl-linux/tools/lkl

mrproper: clean
	rm -rf build

distclean: clean
	rm -rf dist
