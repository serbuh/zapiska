#!/usr/bin/env bash
set -euo pipefail

build_dir="${BUILD_DIR:-build}"
build_type="${BUILD_TYPE:-Debug}"
jobs="${JOBS:-$(nproc)}"

cmake -S . -B "${build_dir}" -DCMAKE_BUILD_TYPE="${build_type}"
cmake --build "${build_dir}" -j"${jobs}"

printf '\nBuilt: %s/src/zapiska/zapiska\n' "${build_dir}"
