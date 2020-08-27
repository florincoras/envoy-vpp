load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
)

licenses(["notice"])  # Apache 2

load("@envoy_api//bazel:api_build_system.bzl", "api_proto_package")

package(default_visibility = ["//visibility:public"])

# VPP Comms Lib (VCL) adaptor.

api_proto_package()

envoy_cc_library(
    name = "vcl_interface_lib",
    srcs = [
        "vcl_event.cc",
        "vcl_interface.cc",
        "vcl_io_handle.cc",
    ],
    hdrs = [
        "vcl_event.h",
        "vcl_interface.h",
        "vcl_io_handle.h",
    ],
    visibility = ["//visibility:public"],
    repository = "@envoy",
    deps = [
        ":pkg_cc_proto",
        "//:vcl_lib",
        "@envoy//include/envoy/event:dispatcher_interface",
        "@envoy//include/envoy/network:socket_interface",
        "@envoy//source/common/common:logger_lib",
        "@envoy//source/common/event:dispatcher_includes",
        "@envoy//source/common/event:dispatcher_lib",
        "@envoy//source/common/event:libevent_scheduler_lib",
        "@envoy//source/common/network:address_lib",
        "@envoy//source/common/network:io_socket_error_lib",
        "@envoy//source/common/network:socket_interface_lib",
        "@envoy//source/common/network:socket_lib",
    ],
)
