#!/usr/bin/env bash

BIN_TYPE=k8-opt
ENVOY_CFG=configs/proxy.yaml
VCL_CFG=configs/vcl.conf

sudo taskset -c 4-7 sh -c "VCL_CONFIG=$VCL_CFG bazel-out/$BIN_TYPE/bin/envoy --concurrency 1 -c $ENVOY_CFG"
