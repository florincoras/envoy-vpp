#!/usr/bin/env bash

if [[ "$TARGETARCH" == "arm64" ]]; then
  return 0
fi

export CC=/opt/llvm-11/bin/clang
export CXX=/opt/llvm-11/bin/clang++
