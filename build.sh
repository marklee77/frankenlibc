#!/bin/sh

MAKE=${MAKE-make}

BUILDDIR=${PWD}/build
STAGEDIR=${BUILDDIR}/stage
LKLSRC=${BUILDDIR}/lkl-linux
OUTDIR=${PWD}/dist
NCPU=1

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
    printf "\t-m: hardcode memory limit. default from env or unlimited\n"
    printf "\t-M: thread stack size. default: 64k\n"
    printf "\t-p: huge page size to use eg 2M or 1G\n"
    printf "\t-r: release build, without debug settings\n"
    printf "\t-o: location of object files. default PWD/build\n"
    printf "\t-d: location of installed files. default PWD/dest\n"
    printf "\t-b: location of binaries. default PWD/dest/bin\n"
    printf "\tseccomp|noseccomp: select Linux seccomp (default off)\n"
    printf "\ttests|notests: select tests (default on)\n"
    printf "\ttools|notools: build extra tools (default on)\n"
    printf "\texecveat: use new linux execveat call default off)\n"
    printf "\tdeterministic: make deterministic\n"
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
        value=$((${value} * 1024 * 1024))
        ;;
    "gb"|"g"|"GB"|"G")
        value=$((${value} * 1024 * 1024 * 1024))
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
        appendvar FRANKEN_FLAGS "-DLKL_MEM_SIZE=${size}"
        ;;
    "o")
        mkdir -p ${OPTARG}
        BUILDDIR=$(abspath ${OPTARG})
        STAGEDIR=${BUILDDIR}/stage
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
appendvar FRANKEN_CFLAGS "-I${STAGEDIR}/lkl-linux/usr/include"

echo "=== building platform-musl ==="
(

    [ -d ${BUILDDIR}/platform-musl ] || git clone  git://git.musl-libc.org/musl ${BUILDDIR}/platform-musl

    set -x
    cd ${BUILDDIR}/platform-musl
    [ -f config.mak ] || ./configure --disable-shared --enable-debug \
        --disable-optimize --prefix=${STAGEDIR}/platform-musl
    make install
)

export CC=${STAGEDIR}/platform-musl/bin/musl-gcc
export HOSTCC=${CC}

echo "=== Linux build LINUX_SRCDIR=${LKLSRC} ==="
(
    [ -d ${LKLSRC} ] || git clone https://github.com/lkl/linux.git ${LKLSRC}

    # FIXME: kind of a hack, do something with modules instead?
    cp kernel/stdio.c ${LKLSRC}/arch/lkl/kernel/stdio.c
    sed -i '$ a obj-y += stdio.o' ${LKLSRC}/arch/lkl/kernel/Makefile

    cd ${LKLSRC}
    set -e
    set -x
    cd tools/lkl
    make -C ../.. ARCH=lkl defconfig
    sed -ie 's/^CONFIG_BTRFS_FS=y/# CONFIG_BTRFS_FS is not set/' ../../.config
    make -C ../.. ARCH=lkl install INSTALL_PATH=$PWD
    make CFLAGS='-Iinclude -Wall -g -O2 -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -fno-strict-aliasing -Wno-missing-field-initializers -fno-strict-aliasing -U_FORTIFY_SOURCE' lib/liblkl.a
    cd ../../
    make headers_install ARCH=lkl O=${STAGEDIR}/lkl-linux
    cp arch/lkl/include/asm/syscalls.h ${STAGEDIR}/lkl-linux/usr/include/asm/

    # undo hack
    rm ${LKLSRC}/arch/lkl/kernel/stdio.c
    sed -i '/obj-y += stdio.o/d' ${LKLSRC}/arch/lkl/kernel/Makefile

    set +e
    set +x
)

echo "=== building lkl-musl ==="
(

    [ -d ${BUILDDIR}/lkl-musl ] || git clone https://github.com/marklee77/musl.git ${BUILDDIR}/lkl-musl

    set -x
    cd ${BUILDDIR}/lkl-musl
    git checkout origin/franken
    LKL_HEADER="${STAGEDIR}/lkl-linux/"
    [ -f config.mak ] || ./configure --with-lkl=${LKL_HEADER} --disable-shared \
            --enable-debug --disable-optimize --prefix=${STAGEDIR}/lkl-musl
    make install
    # install libraries
    #${INSTALL-install} -d ${OUTDIR}/lib
    #${INSTALL-install} ${BUILDDIR}/musl/lib/libpthread.a ${BUILDDIR}/musl/lib/libcrypt.a ${OUTDIR}/lib
)

# install headers
${INSTALL-install} -d ${OUTDIR}/include
cp -a ${STAGEDIR}/lkl-linux/usr/include/* ${OUTDIR}/include
cp -a ${STAGEDIR}/lkl-musl/include/* ${OUTDIR}/include

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
    BUILDDIR="${BUILDDIR}" \
    STAGEDIR="${STAGEDIR}" \
    ${MAKE} ${STDJ} ${OS} -C platform

# should clean up how deterministic build is done
if [ ${DETERMINSTIC-x} = "deterministic" ]
then
    CFLAGS="${EXTRA_CFLAGS} ${DBG_F} ${HUGEPAGESIZE} ${FRANKEN_CFLAGS}" \
    AFLAGS="${EXTRA_AFLAGS} ${DBG_F}" \
    ASFLAGS="${EXTRA_AFLAGS} ${DBG_F}" \
    LDFLAGS="${EXTRA_LDFLAGS}" \
    CPPFLAGS="${EXTRA_CPPFLAGS}" \
    BUILDDIR="${BUILDDIR}" \
    STAGEDIR="${STAGEDIR}" \
    ${MAKE} deterministic -C platform
fi 

CFLAGS="${EXTRA_CFLAGS} ${DBG_F} ${HUGEPAGESIZE} ${FRANKEN_CFLAGS}" \
    AFLAGS="${EXTRA_AFLAGS} ${DBG_F}" \
    ASFLAGS="${EXTRA_AFLAGS} ${DBG_F}" \
    LDFLAGS="${EXTRA_LDFLAGS}" \
    CPPFLAGS="${EXTRA_CPPFLAGS} ${FRANKEN_FLAGS}" \
    LKLSRC="${LKLSRC}" \
    BUILDDIR="${BUILDDIR}" \
    STAGEDIR="${STAGEDIR}" \
    ${MAKE} ${STDJ} -C franken

# explode and implode
rm -rf ${BUILDDIR}/explode
mkdir -p ${BUILDDIR}/explode/libc
mkdir -p ${BUILDDIR}/explode/lkl-musl
mkdir -p ${BUILDDIR}/explode/kernel
mkdir -p ${BUILDDIR}/explode/franken
mkdir -p ${BUILDDIR}/explode/platform
(
    # explode kernel specific libc
    cd ${BUILDDIR}/explode/lkl-musl
    ${AR-ar} x ${STAGEDIR}/lkl-musl/lib/libc.a
    rm -f ${BUILDDIR}/explode/lkl-musl/execve.o
    rm -f ${BUILDDIR}/explode/lkl-musl/posix_spawn.o

    # some franken .o file names conflict with libc
    cd ${BUILDDIR}/explode/franken
    ${AR-ar} x ${STAGEDIR}/lib/libfranken.a
    for f in *.o
    do
        [ -f ../lkl-musl/$f ] && mv $f franken_$f
    done

    # some platform .o file names conflict with libc
    cd ${BUILDDIR}/explode/platform
    ${AR-ar} x ${STAGEDIR}/lib/libplatform.a
    for f in *.o
    do
        [ -f ../lkl-musl/$f ] && mv $f platform_$f
    done

    cd ${BUILDDIR}/explode/kernel
    ${AR-ar} x ${LKLSRC}/tools/lkl/lib/liblkl.a
    rm -f ${BUILDDIR}/explode/kernel/posix-host.o
    rm -f ${BUILDDIR}/explode/kernel/virtio_net.o
    ${CC-cc} ${EXTRA_LDFLAGS} -nostdlib -Wl,-r *.o -o kernel.o

    cd ${BUILDDIR}/explode
    ${AR-ar} cr libc.a kernel/kernel.o lkl-musl/*.o franken/*.o platform/*.o

)

# FIXME: tools need to be ported to musl
unset CC
CPPFLAGS="${EXTRA_CPPFLAGS} ${FILTER}" \
        CFLAGS="${EXTRA_CFLAGS} ${DBG_F} ${FRANKEN_CFLAGS}" \
        LDFLAGS="${EXTRA_LDFLAGS}" \
        LDLIBS="${TOOLS_LDLIBS}" \
        BUILDDIR="${BUILDDIR}" \
        STAGEDIR="${STAGEDIR}" \
        ${MAKE} ${OS} -C tools

# install to OUTDIR
${INSTALL-install} -d ${BINDIR} ${OUTDIR}/lib
${INSTALL-install} ${STAGEDIR}/bin/rexec ${BINDIR}

${INSTALL-install} ${STAGEDIR}/lib/*.o ${OUTDIR}/lib
[ -f ${STAGEDIR}/lib/libg.a ] && ${INSTALL-install} ${STAGEDIR}/lib/libg.a ${OUTDIR}/lib
${INSTALL-install} ${BUILDDIR}/explode/libc.a ${OUTDIR}/lib

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

printf "%s\n\n%s %s %s\n" "#!/bin/sh" \
    "exec ${CC-cc} ${COMPILER_FLAGS} -static -nostdinc -z noexecstack" \
    "-specs ${OUTDIR}/lib/${TOOL_PREFIX}gcc.spec" \
    "-isystem ${OUTDIR}/include \"\$@\"" \
    > ${BINDIR}/${TOOL_PREFIX}-gcc

COMPILER="${TOOL_PREFIX}-gcc"
( cd ${BINDIR}
  ln -s ${COMPILER} ${TOOL_PREFIX}-cc
)
chmod +x ${BINDIR}/${TOOL_PREFIX}-*

# test for duplicated symbols
DUPSYMS=$(nm ${OUTDIR}/lib/libc.a | grep ' T ' | sed 's/.* T //g' | sort | uniq -d )
if [ -n "${DUPSYMS}" ]
then
    printf "WARNING: Duplicate symbols found:\n"
    echo ${DUPSYMS}
fi

# Always make tests to exercise compiler
CC="${BINDIR}/${COMPILER}" \
    OUTDIR="${OUTDIR}" \
    BUILDDIR="${BUILDDIR}" \
    BINDIR="${BINDIR}" \
    ${MAKE} ${STDJ} -C tests

# test for executable stack
readelf -lW ${BUILDDIR}/tests/hello | grep RWE 1>&2 && \
    echo "WARNING: writeable executable section (stack?) found" 1>&2

${MAKE} -C tests/iputils clean
CC="${BINDIR}/${COMPILER}" ${MAKE} -C tests/iputils ping ping6
cp tests/iputils/ping tests/iputils/ping6 build/tests
${MAKE} -C tests/iputils clean

if [ ${RUNTESTS} = "test" ]
then
    CC="${BINDIR}/${COMPILER}" \
        OUTDIR="${OUTDIR}" \
        BUILDDIR="${BUILDDIR}" \
        BINDIR="${BINDIR}" \
        ${MAKE} -C tests run

fi
