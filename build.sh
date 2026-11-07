#!/usr/bin/env bash
# Trails build.
# Deps (Fedora): gtkmm4.0-devel spdlog-devel sqlite-devel cmake gcc-c++ (ninja-build, ccache, mold optional)
# Deps (Ubuntu): libgtkmm-4.0-dev libspdlog-dev libsqlite3-dev cmake g++
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
gen=()
# Ninja only on a fresh build dir -- an existing cache keeps its generator.
if [[ ! -f build/CMakeCache.txt ]] && command -v ninja >/dev/null; then gen=(-G Ninja); fi
cmake -B build "${gen[@]}"
cmake --build build --parallel "$(nproc)" --target trails trails_selftest trails-helper
echo ""
echo "Built: ./build/trails, ./build/trails_selftest, ./build/trails-helper"
