#!/bin/sh
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
# Build a milestone with the QNX SDP 6.5.0 Docker image (qcc, GCC 4.4.2, armv7le).
#
# usage: ./build.sh [milestone-dir] [make targets...]
#   ./build.sh                      # milestone-7, make all
#   ./build.sh milestone-7 rhmiapp  # only the iDrive app
#   ./build.sh milestone-7 clean
#
# Environment:
#   QNX_LICENSE_DIR  (required) directory holding your QNX `licenses` file; mounted at /etc/qnx/license.
#                    It must be writable: qcc creates licenses.lck next to the file.
#   QNX_IMAGE        Docker image with the SDP (default: qnx650-sdk)
set -eu

image=${QNX_IMAGE:-qnx650-sdk}
dir=${1:-milestone-7}
[ $# -gt 0 ] && shift

cd "$(dirname "$0")"

if [ ! -f "$dir/Makefile" ]; then
    echo "build.sh: no Makefile in '$dir'" >&2
    exit 1
fi
if [ -z "${QNX_LICENSE_DIR:-}" ]; then
    echo "build.sh: set QNX_LICENSE_DIR to the directory holding your QNX 'licenses' file" >&2
    exit 1
fi
if [ ! -f "$QNX_LICENSE_DIR/licenses" ]; then
    echo "build.sh: no licenses file in '$QNX_LICENSE_DIR'" >&2
    exit 1
fi
if [ ! -w "$QNX_LICENSE_DIR" ]; then
    echo "build.sh: '$QNX_LICENSE_DIR' is not writable (qcc needs it for licenses.lck)" >&2
    exit 1
fi
if ! docker image inspect "$image" >/dev/null 2>&1; then
    echo "build.sh: Docker image '$image' not found (set QNX_IMAGE)" >&2
    exit 1
fi

[ $# -eq 0 ] && set -- all
exec docker run --rm -u "$(id -u):$(id -g)" \
    -v "$(cd "$dir" && pwd)":/src \
    -v "$(cd "$QNX_LICENSE_DIR" && pwd)":/etc/qnx/license \
    "$image" make "$@"
