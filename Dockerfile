FROM ubuntu:14.04

MAINTAINER Mark Stillwell <mark@stillwell.me>

ENV DEBIAN_FRONTEND noninteractive
RUN apt-get update && \
    apt-get -y install \
        bc \
        build-essential \
        git \
        libseccomp-dev \
        python-minimal && \
    rm -rf /var/lib/apt/lists/* /var/cache/apt/*

ENV SUDO_UID=1000
ENV FRANKEN_VERBOSE=1

RUN useradd -m -s /bin/bash unikernel
RUN cd /usr/src && \
    git clone https://github.com/marklee77/frankenlibc.git && \
    cd frankenlibc && \
    git checkout origin/lkl-musl && \
    ./build.sh -d /usr/local -b /usr/local/bin && \
    cp rumpobj/tests/hello /usr/local/bin/franken.hello && \
    cd .. && \
    rm -rf frankenlibc

CMD ["/usr/local/bin/rexec", "/usr/local/bin/franken.hello"]
