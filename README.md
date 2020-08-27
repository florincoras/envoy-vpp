# Envoy-VPP integration

This is an experimental integration of [Envoy](https://www.envoyproxy.io/) with [VPP](https://fd.io/) done through a VPP Comms Library (VCL) custom Envoy socket interface.

## Building

To build the Envoy static binary with a dynamically linked VCL library:

1. `git submodule update --init`
2. `cd vpp; make install-dep; make build-release; cd ..`
3. `bazel build //:envoy`

For more information on Envoy build requirements check [here](https://github.com/envoyproxy/envoy/blob/14be514c988f46ff38411a859c401b5cce4b4b3f/bazel/README.md). This was tested with prebuilt clang-9 packages from [LLVM official site](http://releases.llvm.org/download.html) and after:

```
export CC=/path/to/clang
export CXX=/path/to/clang++
```

If step 2 above fails, check VPP's developer documentation [here](https://fd.io/docs/vpp/master/gettingstarted/developers/index.html).

## Run

After updating VPP's example [startup configuration](configs/vpp_startup.conf), to start Envoy as a HTTP proxy using VPP's user space networking stack:

1. `sudo ./vpp/build-root/install-vpp-native/vpp/bin/vpp -c configs/vpp_startup.conf`
2. `./start_envoy.sh`

To check that everything started successfuly `show session verbose` in VPP's cli should return one listening session on the proxy port configured in [proxy](configs/proxy.yaml) (default 10001). Both the address and the port of the proxy service should be updated to those of the actual HTTP server.

VPP's example startup configuration assumes only one physical interface and a tap interface to be used to communicate with a local HTTP server using the Linux network stack.
