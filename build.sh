#!/bin/bash
# Convenience build script: bootstrap + configure + make.
#
# Usage:
#   ./build.sh [options]
#
# Options:
#   --vulkan        Configure with --with-vulkan=yes (requires libvulkan,
#                    vulkan headers, and glslangValidator/glslc)
#   --debug         Build with debug symbols, no optimization (-g -O0)
#   --clean         make clean before building
#   --reconfigure   Force re-running bootstrap.sh + configure even if
#                    configure/Makefile already exist
#   --jobs=N        Parallel build jobs (default: nproc)
#   -h, --help      Show this help
#
# Any extra arguments after a literal '--' are passed straight through to
# ./configure, e.g.:
#   ./build.sh --vulkan -- --with-glew=yes

set -euo pipefail

cd "$(dirname "$0")"

VULKAN=false
DEBUG=false
CLEAN=false
RECONFIGURE=false
JOBS="$(nproc 2>/dev/null || echo 4)"
EXTRA_CONFIGURE_ARGS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --vulkan) VULKAN=true ;;
        --debug) DEBUG=true ;;
        --clean) CLEAN=true ;;
        --reconfigure) RECONFIGURE=true ;;
        --jobs=*) JOBS="${1#--jobs=}" ;;
        -h|--help)
            sed -n '2,21p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        --)
            shift
            EXTRA_CONFIGURE_ARGS+=("$@")
            break
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
    shift
done

CONFIGURE_ARGS=()
$VULKAN && CONFIGURE_ARGS+=(--with-vulkan=yes)
CONFIGURE_ARGS+=("${EXTRA_CONFIGURE_ARGS[@]}")

if [ ! -x ./configure ] || $RECONFIGURE; then
    echo "==> Running bootstrap.sh"
    ./bootstrap.sh
fi

if [ ! -f Makefile ] || $RECONFIGURE; then
    echo "==> Running configure ${CONFIGURE_ARGS[*]:-}"
    ./configure "${CONFIGURE_ARGS[@]}"
fi

MAKE_CXXFLAGS=()
if $DEBUG; then
    # Overriding CXXFLAGS on the make command line replaces the value
    # configure baked into the Makefile (e.g. -std=c++23), so it must be
    # repeated here rather than just appending -g -O0.
    STD_FLAG="$(grep -m1 '^CXXFLAGS' Makefile | grep -o 'std=[a-zA-Z0-9+]*' | head -1)"
    MAKE_CXXFLAGS=(CXXFLAGS="-${STD_FLAG:-std=c++23} -g -O0")
fi

if $CLEAN; then
    echo "==> make clean"
    make clean -C src
fi

echo "==> Building (jobs=$JOBS)"
make -j"$JOBS" -C src "${MAKE_CXXFLAGS[@]}"

echo "==> Done: src/armagetronad_main"
