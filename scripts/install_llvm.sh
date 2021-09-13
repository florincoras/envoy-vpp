#!/usr/bin/env bash

if [[ "$TARGETARCH" != "arm64" ]]; then
  wget https://github.com/llvm/llvm-project/releases/download/llvmorg-11.1.0/clang+llvm-11.1.0-$TARGETARCH-linux-gnu-ubuntu-20.10.tar.xz -O /opt/llvm11.tar.xz && cd /opt && tar xf llvm11.tar.xz && mv clang+llvm-11.1.0-$TARGETARCH-linux-gnu-ubuntu-20.10 llvm-11 && rm llvm11.tar.xz
fi
