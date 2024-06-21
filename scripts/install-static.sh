#!/usr/bin/env sh
set -e

fetch(){
    url=$1
    wget $url
    tar xf $(basename $url)
    rm $(basename $url)
}

configure_install() {
    saved_dir=$(pwd)
    cd $1
    shift 1
    if [ -f "autogen.sh" ]; then
        sh autogen.sh
    fi

    ./configure --disable-shared $@
    make -j
    make install
    cd $saved_dir
}

cmake_install(){
    saved_dir=$(pwd)
    cd $1
    shift 1
    cmake -S . -B build -G Ninja -D BUILD_SHARED_LIBS=OFF $@
    cmake --build build
    cmake --build build --target install
    cd $saved_dir
}

configure_install_package() {
    dir_name=$1
    url=$2
    shift 2
    fetch $url
    configure_install $dir_name $@
}

cmake_install_package() {
    dir_name=$1
    url=$2
    fetch $url
    shift 2
    cmake_install $dir_name $@
}

# install libb2
configure_install_package "libb2-0.98.1" "https://github.com/BLAKE2/libb2/releases/download/v0.98.1/libb2-0.98.1.tar.gz" --disable-dependency-tracking
# install pcre2
configure_install_package "pcre2-10.44" "https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.44/pcre2-10.44.tar.gz" --enable-pcre2-16
# install double-conversion
cmake_install_package "double-conversion-3.3.0" "https://github.com/google/double-conversion/archive/refs/tags/v3.3.0.tar.gz"
