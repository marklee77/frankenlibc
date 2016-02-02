#!/bin/sh

MAKE=${MAKE-make}

RUMPOBJ=${PWD}/rumpobj
RUMP=${RUMPOBJ}/rump
RUMPSRC=${PWD}/src
LKLSRC=${PWD}/../lkl-linux
OUTDIR=${PWD}/rump
NCPU=1
RUMP_KERNEL=linux

EXTRA_AFLAGS="-Wa,--noexecstack"

TARGET=$(LC_ALL=C ${CC-cc} -v 2>&1 | sed -n 's/^Target: //p' )

case ${TARGET} in
*-linux*)
	OS=linux
	;;
*-netbsd*)
	OS=netbsd
	;;
*-freebsd*)
	OS=freebsd
	FILTER="-DCAPSICUM"
	;;
*)
	OS=unknown
esac

HOST=$(uname -s)

case ${HOST} in
Linux)
	NCPU=$(nproc 2>/dev/null || echo 1)
	;;
NetBSD)
	NCPU=$(sysctl -n hw.ncpu)
	;;
FreeBSD)
	NCPU=$(sysctl -n hw.ncpu)
	;;
esac

STDJ="-j ${NCPU}"
BUILD_QUIET="-qq"
DBG_F='-O2 -g'

helpme()
{
	printf "Usage: $0 [-h] [options] [platform]\n"
	printf "supported options:\n"
	printf "\t-L: libraries to link eg net_netinet,net_netinet6. default all\n"
	printf "\t-m: hardcode rump memory limit. default from env or unlimited\n"
	printf "\t-M: thread stack size. default: 64k\n"
	printf "\t-p: huge page size to use eg 2M or 1G\n"
	printf "\t-r: release build, without debug settings\n"
	printf "\t-s: location of source tree.  default: PWD/rumpsrc\n"
	printf "\t-o: location of object files. default PWD/rumpobj\n"
	printf "\t-d: location of installed files. default PWD/rump\n"
	printf "\t-b: location of binaries. default PWD/rump/bin\n"
	printf "\tseccomp|noseccomp: select Linux seccomp (default off)\n"
	printf "\texecveat: use new linux execveat call default off)\n"
	printf "\tcapsicum|nocapsicum: select FreeBSD capsicum (default on)\n"
	printf "\tdeterministic: make deterministic\n"
	printf "\tnotests: do not run tests\n"
	printf "\tnotools: do not build extra tools\n"
	printf "\tclean: clean object directory first\n"
	printf "\n"
	printf "Supported platforms are currently: linux, netbsd, freebsd, qemu-arm, spike\n"
	exit 1
}

bytes()
{
	value=$(echo "$1" | sed 's/[^0123456789].*$//g')
	units=$(echo "$1" | sed 's/^[0123456789]*//g')

	case "$units" in
	"kb"|"k"|"KB"|"K")
		value=$((${value} * 1024))
		;;
	"mb"|"m"|"MB"|"M")
		value=$((${value} * 1048576))
		;;
	"gb"|"g"|"GB"|"G")
		value=$((${value} * 1073741824))
		;;
	*)
		die "Cannot understand value"
		;;
	esac

	echo ${value}
}

abspath() {
    cd "$1"
    printf "$(pwd)"
}

appendvar_fs ()
{
	vname="${1}"
	fs="${2}"
	shift 2
	if [ -z "$(eval echo \${$vname})" ]; then
		eval ${vname}="\${*}"
	else
		eval ${vname}="\"\${${vname}}"\${fs}"\${*}\""
	fi
}

appendvar ()
{

	vname="$1"
	shift
	appendvar_fs "${vname}" ' ' $*
}

while getopts '?b:d:F:Hhj:k:L:M:m:o:p:qrs:V:' opt; do
	case "$opt" in
	"b")
		mkdir -p ${OPTARG}
		BINDIR=$(abspath ${OPTARG})
		;;
	"d")
		mkdir -p ${OPTARG}
		OUTDIR=$(abspath ${OPTARG})
		;;
	"F")
		ARG=${OPTARG#*=}
		case ${OPTARG} in
			CFLAGS\=*)
				appendvar EXTRA_CFLAGS "${ARG}"
				;;
			AFLAGS\=*)
				appendvar EXTRA_AFLAGS "${ARG}"
				;;
			LDFLAGS\=*)
				appendvar EXTRA_LDFLAGS "${ARG}"
				;;
			ACFLAGS\=*)
				appendvar EXTRA_CFLAGS "${ARG}"
				appendvar EXTRA_AFLAGS "${ARG}"
				;;
			ACLFLAGS\=*)
				appendvar EXTRA_CFLAGS "${ARG}"
				appendvar EXTRA_AFLAGS "${ARG}"
				appendvar EXTRA_LDFLAGS "${ARG}"
				;;
			CPPFLAGS\=*)
				appendvar EXTRA_CPPFLAGS "${ARG}"
				;;
			DBG\=*)
				appendvar F_DBG "${ARG}"
				;;
			CWARNFLAGS\=*)
				appendvar EXTRA_CWARNFLAGS "${ARG}"
				;;
			*)
				die Unknown flag: ${OPTARG}
				;;
		esac
		;;
	"H")
		appendvar EXTRAFLAGS "-H"
		;;
	"h")
		helpme
		;;
	"j")
		STDJ="-j ${OPTARG}"
		;;
	"L")
		LIBS="${OPTARG}"
		;;
	"M")
		size=$(bytes ${OPTARG})
		appendvar FRANKEN_FLAGS "-DSTACKSIZE=${size}"
		;;
	"o")
		mkdir -p ${OPTARG}
		RUMPOBJ=$(abspath ${OPTARG})
		RUMP=${RUMPOBJ}/rump
		;;
	"p")
		SIZE=$(bytes ${OPTARG})
		HUGEPAGESIZE="-DHUGEPAGESIZE=${SIZE}"
		;;
	"q")
		BUILD_QUIET=${BUILD_QUIET:=-}q
		;;
	"r")
		DBG_F="-O2"
		appendvar EXTRAFLAGS "-r"
		;;
	"s")
		RUMPSRC=${OPTARG}
		;;
	"V")
		appendvar EXTRAFLAGS "-V ${OPTARG}"
		;;
	"?")
		helpme
		;;
	esac
done
shift $((${OPTIND} - 1))

if [ -z ${BINDIR+x} ]; then BINDIR=${OUTDIR}/bin; fi

for arg in "$@"; do
        case ${arg} in
	"clean")
		${MAKE} clean
		;;
	"noseccomp")
		;;
	"seccomp")
		appendvar FILTER "-DSECCOMP"
		appendvar TOOLS_LDLIBS "-lseccomp"
		;;
	"noexecveat")
		;;
	"execveat")
		appendvar FILTER "-DEXECVEAT"
		;;
	"nocapsicum")
		FILTER="-DNOCAPSICUM"
		;;
	"capsicum")
		FILTER="-DCAPSICUM"
		;;
	"deterministic"|"det")
		DETERMINISTIC="deterministic"
		;;
	"test"|"tests")
		RUNTESTS="test"
		;;
	"notest"|"notests")
		RUNTESTS="notest"
		;;
	"tools")
		MAKETOOLS="yes"
		;;
	"notools")
		MAKETOOLS="no"
		;;
	*)
		OS=${arg}
		;;
	esac
done

set -e

if [ "${OS}" = "unknown" ]; then
	die "Unknown or unsupported platform"
fi

[ -f platform/${OS}/platform.sh ] && . platform/${OS}/platform.sh

RUNTESTS="${RUNTESTS-test}"
MAKETOOLS="${MAKETOOLS-yes}"

rm -rf ${OUTDIR}

FRANKEN_CFLAGS="-std=c99 -Wall -Wextra -Wno-missing-braces -Wno-unused-parameter -Wno-missing-field-initializers -D__KERNEL__"

if [ "${HOST}" = "Linux" ]; then appendvar FRANKEN_CFLAGS "-D_GNU_SOURCE"; fi

appendvar FRANKEN_CFLAGS "-I${LKLSRC}/tools/lkl/include"

CPPFLAGS="${EXTRA_CPPFLAGS} ${FILTER}" \
        CFLAGS="${EXTRA_CFLAGS} ${DBG_F} ${FRANKEN_CFLAGS}" \
        LDFLAGS="${EXTRA_LDFLAGS}" \
        LDLIBS="${TOOLS_LDLIBS}" \
        RUMPOBJ="${RUMPOBJ}" \
        RUMP="${RUMP}" \
        ${MAKE} ${OS} -C tools

# build lkl
echo "=== Linux build LINUX_SRCDIR=${LKLSRC} ==="
(
	cd ${LKLSRC}
	set -e
	set -x
	cd tools/lkl
	make clean
	make 
	cd ../../
	make headers_install ARCH=lkl O=${RUMP}/lkl-linux
    cp arch/lkl/include/asm/syscalls.h ${RUMP}/lkl-linux/usr/include/asm/
	set +e
	set +x
)

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

# install headers
${INSTALL-install} -d ${OUTDIR}/include
cp -a ${RUMP}/lkl-linux/usr/include/* ${OUTDIR}/include
cp -a ${RUMPOBJ}/musl/include/* ${OUTDIR}/include

## FIXME: MUSL_LIBC is somehow misleading as franken also uses.
## LINUX_LIBC?
appendvar FRANKEN_FLAGS "-DMUSL_LIBC"
appendvar EXTRA_CPPFLAGS "-DCONFIG_LKL"
appendvar EXTRA_CFLAGS "-DCONFIG_LKL"

CFLAGS="${EXTRA_CFLAGS} ${DBG_F} ${HUGEPAGESIZE} ${FRANKEN_CFLAGS}" \
	AFLAGS="${EXTRA_AFLAGS} ${DBG_F}" \
	ASFLAGS="${EXTRA_AFLAGS} ${DBG_F}" \
	LDFLAGS="${EXTRA_LDFLAGS}" \
	CPPFLAGS="${EXTRA_CPPFLAGS}" \
	RUMPOBJ="${RUMPOBJ}" \
	RUMP="${RUMP}" \
	${MAKE} ${STDJ} ${OS} -C platform

# should clean up how deterministic build is done
if [ ${DETERMINSTIC-x} = "deterministic" ]
then
	CFLAGS="${EXTRA_CFLAGS} ${DBG_F} ${HUGEPAGESIZE} ${FRANKEN_CFLAGS}" \
	AFLAGS="${EXTRA_AFLAGS} ${DBG_F}" \
	ASFLAGS="${EXTRA_AFLAGS} ${DBG_F}" \
	LDFLAGS="${EXTRA_LDFLAGS}" \
	CPPFLAGS="${EXTRA_CPPFLAGS}" \
	RUMPOBJ="${RUMPOBJ}" \
	RUMP="${RUMP}" \
	${MAKE} deterministic -C platform
fi 

CFLAGS="${EXTRA_CFLAGS} ${DBG_F} ${HUGEPAGESIZE} ${FRANKEN_CFLAGS}" \
	AFLAGS="${EXTRA_AFLAGS} ${DBG_F}" \
	ASFLAGS="${EXTRA_AFLAGS} ${DBG_F}" \
	LDFLAGS="${EXTRA_LDFLAGS}" \
	CPPFLAGS="${EXTRA_CPPFLAGS} ${FRANKEN_FLAGS}" \
    LKLSRC="${LKLSRC}" \
	RUMPOBJ="${RUMPOBJ}" \
	RUMP="${RUMP}" \
	${MAKE} ${STDJ} -C franken

#CFLAGS="${EXTRA_CFLAGS} ${DBG_F} ${FRANKEN_CFLAGS}" \
#	LDFLAGS="${EXTRA_LDFLAGS}" \
#	CPPFLAGS="${EXTRA_CPPFLAGS} ${RUMPUSER_FLAGS}" \
#	RUMPOBJ="${RUMPOBJ}" \
#	RUMP="${RUMP}" \
#	${MAKE} ${STDJ} -C libvirtif

ALL_LIBS=${LKLSRC}/tools/lkl/lib/liblkl.a

# explode and implode
rm -rf ${RUMPOBJ}/explode
mkdir -p ${RUMPOBJ}/explode/libc
mkdir -p ${RUMPOBJ}/explode/musl
mkdir -p ${RUMPOBJ}/explode/rumpkernel
mkdir -p ${RUMPOBJ}/explode/franken
mkdir -p ${RUMPOBJ}/explode/platform
(
	# explode rumpkernel specific libc
	cd ${RUMPOBJ}/explode/musl
	${AR-ar} x ${RUMPOBJ}/musl/lib/libc.a

	# some franken .o file names conflict with libc
	cd ${RUMPOBJ}/explode/franken
	${AR-ar} x ${RUMP}/lib/libfranken.a
	for f in *.o
	do
		[ -f ../musl/$f ] && mv $f franken_$f
	done

	# some platform .o file names conflict with libc
	cd ${RUMPOBJ}/explode/platform
	${AR-ar} x ${RUMP}/lib/libplatform.a
	for f in *.o
	do
		[ -f ../libc/$f ] && mv $f platform_$f
	done

	cd ${RUMPOBJ}/explode/rumpkernel
	for f in ${ALL_LIBS}
	do
		${AR-ar} x $f
	done
	${CC-cc} ${EXTRA_LDFLAGS} -nostdlib -Wl,-r *.o -o kernel.o

	cd ${RUMPOBJ}/explode
	${AR-ar} cr libc.a rumpkernel/kernel.o musl/*.o franken/*.o platform/*.o
)

# install to OUTDIR
${INSTALL-install} -d ${BINDIR} ${OUTDIR}/lib
${INSTALL-install} ${RUMP}/bin/rexec ${BINDIR}

${INSTALL-install} ${RUMP}/lib/*.o ${OUTDIR}/lib
[ -f ${RUMP}/lib/libg.a ] && ${INSTALL-install} ${RUMP}/lib/libg.a ${OUTDIR}/lib
${INSTALL-install} ${RUMPOBJ}/explode/libc.a ${OUTDIR}/lib

# create toolchain wrappers
# select these based on compiler defs
UNDEF=""
TOOL_PREFIX=franken
COMPILER_FLAGS="-fno-stack-protector ${EXTRA_CFLAGS}"
COMPILER_FLAGS="$(echo ${COMPILER_FLAGS} | sed 's/--sysroot=[^ ]*//g')"
[ -f ${OUTDIR}/lib/crt0.o ] && appendvar STARTFILE "${OUTDIR}/lib/crt0.o"
[ -f ${OUTDIR}/lib/crt1.o ] && appendvar STARTFILE "${OUTDIR}/lib/crt1.o"
appendvar STARTFILE "${OUTDIR}/lib/crti.o"
[ -f ${OUTDIR}/lib/crtbegin.o ] && appendvar STARTFILE "${OUTDIR}/lib/crtbegin.o"
[ -f ${OUTDIR}/lib/crtbeginT.o ] && appendvar STARTFILE "${OUTDIR}/lib/crtbeginT.o"
ENDFILE="${OUTDIR}/lib/crtend.o ${OUTDIR}/lib/crtn.o"
cat tools/spec.in | sed \
    -e "s#@SYSROOT@#${OUTDIR}#g" \
    -e "s#@CPPFLAGS@#${EXTRA_CPPFLAGS}#g" \
    -e "s#@AFLAGS@#${EXTRA_AFLAGS}#g" \
    -e "s#@CFLAGS@#${EXTRA_CFLAGS}#g" \
    -e "s#@LDFLAGS@#${EXTRA_LDFLAGS}#g" \
    -e "s#@LDSCRIPT@#${EXTRA_LDSCRIPT}#g" \
    -e "s#@UNDEF@#${UNDEF}#g" \
    -e "s#@STARTFILE@#${STARTFILE}#g" \
    -e "s#@ENDFILE@#${ENDFILE}#g" \
    -e "s/--sysroot=[^ ]*//" \
    > ${OUTDIR}/lib/${TOOL_PREFIX}gcc.spec
printf "#!/bin/sh\n\nexec ${CC-cc} -specs ${OUTDIR}/lib/${TOOL_PREFIX}gcc.spec ${COMPILER_FLAGS} -static -nostdinc -isystem ${OUTDIR}/include \"\$@\"\n" > ${BINDIR}/${TOOL_PREFIX}-gcc
COMPILER="${TOOL_PREFIX}-gcc"
( cd ${BINDIR}
  ln -s ${COMPILER} ${TOOL_PREFIX}-cc
  ln -s ${COMPILER} rumprun-cc
)
printf "#!/bin/sh\n\nexec ${AR-ar} \"\$@\"\n" > ${BINDIR}/${TOOL_PREFIX}-ar
printf "#!/bin/sh\n\nexec ${NM-nm} \"\$@\"\n" > ${BINDIR}/${TOOL_PREFIX}-nm
printf "#!/bin/sh\n\nexec ${OBJCOPY-objcopy} \"\$@\"\n" > ${BINDIR}/${TOOL_PREFIX}-objcopy
chmod +x ${BINDIR}/${TOOL_PREFIX}-*

# test for duplicated symbols

DUPSYMS=$(nm ${OUTDIR}/lib/libc.a | grep ' T ' | sed 's/.* T //g' | sort | uniq -d )

if [ -n "${DUPSYMS}" ]
then
	printf "WARNING: Duplicate symbols found:\n"
	echo ${DUPSYMS}
	#exit 1
fi

# Always make tests to exercise compiler
CC="${BINDIR}/${COMPILER}" \
	RUMPDIR="${OUTDIR}" \
	RUMPOBJ="${RUMPOBJ}" \
	BINDIR="${BINDIR}" \
	${MAKE} ${STDJ} -C tests

# test for executable stack
readelf -lW ${RUMPOBJ}/tests/hello | grep RWE 1>&2 && echo "WARNING: writeable executable section (stack?) found" 1>&2

#${MAKE} -C tests/iputils clean
#CC="${BINDIR}/${COMPILER}" ${MAKE} -C tests/iputils ping ping6

if [ ${RUNTESTS} = "test" ]
then
	CC="${BINDIR}/${COMPILER}" \
		RUMPDIR="${OUTDIR}" \
		RUMPOBJ="${RUMPOBJ}" \
		BINDIR="${BINDIR}" \
		${MAKE} -C tests run

fi
