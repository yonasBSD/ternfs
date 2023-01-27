# The image we use to build the static "alpine" binaries
# that we deploy.
FROM alpine:3.17
RUN apk add --no-cache \
    bash perl coreutils python3 musl gcc g++ clang lld make cmake ninja mandoc \
    linux-headers zlib-dev zlib-static elfutils-dev libelf-static libdwarf-dev libdwarf-static \
    xz-dev xz-static xxhash-dev libunwind-dev libunwind-static
ENV IN_EGGS_BUILD_CONTAINER Y
