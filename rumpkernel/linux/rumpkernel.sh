#!/bin/sh

rumpkernel_buildrump()
{
	echo "=== Linux build LINUX_SRCDIR=${LKLSRC} ==="
	cd ${LKLSRC}
	set -e
	set -x
	cd tools/lkl
	make clean
	make 
	cd ../../
	make headers_install ARCH=lkl O=${RUMP}/lkl-linux
	set +e
	set +x

    cd ../frankenlibc

}

rumpkernel_createuserlib()
{
# build musl libc for Linux
(
	set -x
	echo "=== building musl ==="
	cd ../musl
	LKL_HEADER="${RUMP}/lkl-linux/"
	CIRCLE_TEST_REPORTS="${CIRCLE_TEST_REPORTS-./}"
	./configure --with-lkl=${LKL_HEADER} --disable-shared --enable-debug \
		    --disable-optimize --prefix=${RUMPOBJ}/musl 2>&1 | tee $CIRCLE_TEST_REPORTS/log-conf.txt
	make install 2>&1 | tee $CIRCLE_TEST_REPORTS/log-make.txt
	# install libraries
	${INSTALL-install} -d ${OUTDIR}/lib
	${INSTALL-install} ${RUMPOBJ}/musl/lib/libpthread.a ${RUMPOBJ}/musl/lib/libcrypt.a ${OUTDIR}/lib
)
}

rumpkernel_install_header()
{
	## FIXME: MUSL_LIBC is somehow misleading as franken also uses.
	## LINUX_LIBC?
	appendvar FRANKEN_FLAGS "-DMUSL_LIBC"
	appendvar EXTRA_CPPFLAGS "-DCONFIG_LKL"
	appendvar EXTRA_CFLAGS "-DCONFIG_LKL"

	# install headers
	cp -a ${RUMP}/lkl-linux/usr/include/* ${OUTDIR}/include
	cp -a ${RUMPOBJ}/musl/include/* ${OUTDIR}/include

}

rumpkernel_install_extra_libs ()
{
	return 0
}

rumpkernel_explode_libc()
{
(
	cd ${RUMPOBJ}/explode/musl
	${AR-ar} x ${RUMPOBJ}/musl/lib/libc.a
)
	LIBC_DIR=musl

}

rumpkernel_build_extra()
{
	RUMPSRC=${RUMPSRC} \
	RUMPOBJ=${RUMPOBJ} \
	OUTDIR=${OUTDIR} \
	BINDIR=${BINDIR} \
		${MAKE} ${STDJ} -C rumpkernel/linux
}

rumpkernel_maketools()
{
	return 0
}

rumpkernel_build_test()
{
	OBJDIR=${RUMPOBJ}/tests

	# XXX qemu-arm still has an issue on pthread_self() during setuid
	#if [ ${OS} != "qemu-arm" ] ;
	#then
	#	${MAKE} -C tests/iputils clean
	#	CC="${BINDIR}/${COMPILER}" ${MAKE} -C tests/iputils ping ping6
	#	cp tests/iputils/ping tests/iputils/ping6 ${OBJDIR}/
	#	${MAKE} -C tests/iputils clean
	#fi
}
