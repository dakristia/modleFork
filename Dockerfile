# Copyright (C) 2021 Roberto Rossini <roberros@uio.no>
#
# SPDX-License-Identifier: MIT

#FROM conanio/gcc10-ubuntu16.04:1.41.0 AS builder
FROM conanio/gcc11-ubuntu16.04:1.41.0 AS builder

ARG src_dir='/home/conan/modle'
ARG build_dir='/home/conan/modle/build'
ARG staging_dir='/home/conan/modle/staging'
ARG install_dir='/usr/local'

ARG LIBBIGWIG_VER=0.4.6
ARG THREAD_POOL_VER=2.0.0
ARG XOSHIRO_CPP_VER=1.1

ARG CONAN_V2=1
ARG CONAN_REVISIONS_ENABLED=1
ARG CONAN_NON_INTERACTIVE=1
ARG CONAN_CMAKE_GENERATOR=Ninja

RUN sudo apt-get update                                \
    && sudo apt-get install -y --no-install-recommends \
                            ninja-build

RUN mkdir -p "$src_dir" "$build_dir"

COPY conanfile.py "$src_dir"

RUN sudo chown -R conan "$src_dir"

RUN cd "$build_dir"                              \
    && conan install "$src_dir/conanfile.py"     \
                  --build outdated               \
                  -s compiler.cppstd=17          \
                  -s build_type=Release          \
                  -s compiler.libcxx=libstdc++11 \
                  -o enable_testing=ON

COPY LICENSE                "$src_dir/LICENSE"
COPY "external/libBigWig-$LIBBIGWIG_VER.tar.xz"                \
     "$src_dir/external/libBigWig-$LIBBIGWIG_VER.tar.xz"
COPY "external/mscharconv.tar.xz"                              \
     "$src_dir/external/mscharconv.tar.xz"
COPY "external/thread-pool-$THREAD_POOL_VER.tar.xz"            \
     "$src_dir/external/thread-pool-$THREAD_POOL_VER.tar.xz"
COPY "external/Xoshiro-cpp-$XOSHIRO_CPP_VER.tar.xz"            \
     "$src_dir/external/Xoshiro-cpp-$XOSHIRO_CPP_VER.tar.xz"
COPY cmake                  "$src_dir/cmake"
COPY CMakeLists.txt         "$src_dir/CMakeLists.txt"
COPY test                   "$src_dir/test"
COPY src                    "$src_dir/src"

RUN sudo chown -R conan "$src_dir"
RUN cd "$build_dir"                                \
    && cmake -DCMAKE_BUILD_TYPE=Release            \
             -DENABLE_IPO=ON                       \
             -DWARNINGS_AS_ERRORS=ON               \
             -DENABLE_TESTING=ON                   \
             -DCMAKE_INSTALL_PREFIX="$staging_dir" \
             -G Ninja                              \
             "$src_dir"

RUN cd "$build_dir"                   \
    && cmake --build . -j "$(nproc)"  \
    && cmake --install .

FROM ubuntu:20.04 AS testing

ARG SCIPY_VER="1.5.1"
ARG src_dir="/home/conan/modle"

RUN sudo apt-get update \
    && sudo apt-get install -y --no-install-recommends \
                            python3-pip           \
    && pip3 install "scipy==${SCIPY_VER}"

COPY --from=builder "$src_dir" "$src_dir"
COPY --from=builder "/usr/bin/ctest" "/usr/bin/ctest"

RUN cd "$src_dir/build"               \
    && ctest -j "$(nproc)"            \
             --test-dir .             \
             --schedule-random        \
             --output-on-failure      \
             --no-tests=error         \
    && rm -rf "$src_dir/test/Testing"

FROM ubuntu:20.04 AS base

ARG staging_dir='/home/conan/modle/staging'
ARG install_dir='/usr/local'
ARG ver

COPY --from=builder "$staging_dir" "$install_dir"

LABEL maintainer='Roberto Rossini <roberros@uio.no>'
LABEL version="$ver"
WORKDIR /data
ENTRYPOINT ["/usr/local/bin/modle"]

RUN modle --help
RUN modle_tools --help
