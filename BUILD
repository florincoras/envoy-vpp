package(default_visibility = ["//visibility:public"])

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_cc_test",
)

genrule(
    name = "vpp_build",
    srcs = ["vpp/build-root/install-vpp-native/vpp/include/vcl/vppcom.h"],
    outs = ["vpp/include/vcl/vppcom.h"],
    cmd = "cp $(SRCS) $@",
)

cc_library(
    name = "vcl_lib",
    srcs = ["vpp/build-root/install-vpp-native/vpp/lib/libvppcom.so.21.06"],
    hdrs = [":vpp_build"],
    includes = ["vpp/build-root/install-vpp-native/vpp/include/"],
)

envoy_cc_binary(
    name = "envoy",
    repository = "@envoy",
    deps = [
        "//vcl:vcl_interface_lib",
        "@envoy//source/exe:envoy_main_entry_lib",
    ],
)

