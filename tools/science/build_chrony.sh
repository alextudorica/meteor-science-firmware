#!/bin/sh
# Build chronyd and chronyc for a Hi3516CV300 camera running OpenIPC (ARM926EJ-S, musl).
# Usage: CC=/path/to/arm-openipc-linux-musleabi-gcc tools/science/build_chrony.sh [workdir]
# Output: <workdir>/chrony-4.6.1/chronyd and chronyc (stripped). chrony is GPL-2.0; this script
# only fetches and builds the upstream release.
set -eu
: "${CC:?set CC to the OpenIPC musl cross compiler}"
VER=4.6.1
SHA256=571ff73fbf0ae3097f0604eca2e00b1d8bb2e91affe1a3494785ff21d6199c5c
WORK=${1:-build-chrony}
mkdir -p "$WORK" && cd "$WORK"
[ -f chrony-$VER.tar.gz ] || curl -sSfL -o chrony-$VER.tar.gz https://chrony-project.org/releases/chrony-$VER.tar.gz
echo "$SHA256  chrony-$VER.tar.gz" | sha256sum -c -
rm -rf chrony-$VER && tar xzf chrony-$VER.tar.gz && cd chrony-$VER
CFLAGS=-Os ./configure --host-system=Linux --host-release=3.18 --host-machine=armv5tejl \
    --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
    --disable-readline --disable-sechash --disable-nts --disable-privdrop --disable-scfilter \
    --without-libcap --without-seccomp --without-editline --without-nettle --without-nss \
    --without-tomcrypt --without-gnutls --disable-phc --disable-pps --disable-refclock --disable-ipv6
make -j"$(nproc)" chronyd chronyc
"${CC%gcc}strip" chronyd chronyc
ls -l chronyd chronyc
