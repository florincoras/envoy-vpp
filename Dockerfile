ARG UBUNTU_VERSION=20.10

FROM ubuntu:${UBUNTU_VERSION} as base
ARG TARGETARCH=arm64
RUN apt update
RUN DEBIAN_FRONTEND=noninteractive TZ=US/Central apt-get install -y git make python3 sudo wget clang-10
ADD install_llvm.sh /tmp/install_llvm.sh
RUN /tmp/install_llvm.sh
RUN unset CC
RUN unset CXX
RUN git clone https://github.com/florincoras/envoy-vpp.git
RUN cd /envoy-vpp && git submodule update --init
RUN cd /envoy-vpp/vpp && DEBIAN_FRONTEND=noninteractive TZ=US/Central UNATTENDED=y make install-dep
RUN wget https://github.com/bazelbuild/bazel/releases/download/4.2.1/bazel-4.2.1-linux-$TARGETARCH -O /bin/bazel && chmod +x /bin/bazel

FROM base as vppbuild
RUN cd /envoy-vpp/vpp && make build-release

FROM base as envoybase
COPY --from=vppbuild /envoy-vpp/vpp/build-root/install-vpp-* /envoy-vpp/vpp/build-root/install-vpp-native/
ADD set_cc.sh /tmp/set_cc.sh
RUN /tmp/set_cc.sh

FROM envoybase as envoybuild
RUN cd /envoy-vpp && bazel build //:envoy

