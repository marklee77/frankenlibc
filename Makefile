default:	
		./build.sh

seccomp:
		./build.sh seccomp

qemu-arm:
		./build.sh qemu-arm

spike:
		./build.sh spike

PHONY:		clean distclean qemu-arm spike seccomp

clean:		
		rm -rf rumpobj
		make clean -C ../musl
		make clean -C ../lkl-linux
		make clean -C ../lkl-linux/tools/lkl

distclean:	clean
		rm -rf rump
