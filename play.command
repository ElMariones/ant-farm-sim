#!/bin/sh
# Normal play uses the optimized build; the dev preset remains available for debugging.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$project_dir"
cmake --preset release
cmake --build --preset release
exec "$project_dir/build/release/ant_farm" "$@"
