#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${script_dir}"

build_dir="${BUILD_DIR:-build}"
binary="${build_dir}/src/zapiska/zapiska"

if [[ ! -x "${binary}" ]]; then
    ./build.sh
fi

exec "./${binary}" "$@"
