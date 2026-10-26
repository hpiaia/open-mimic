#!/bin/sh
# Open verified pad measurements in the official Rerun viewer.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
inspection_env="$project_dir/build/inspection-venv"
requirements="$project_dir/scripts/requirements-inspection.txt"
if [ ! -x "$inspection_env/bin/python" ]; then
    python3 -m venv "$inspection_env"
fi
if ! cmp -s "$requirements" "$inspection_env/.requirements"; then
    "$inspection_env/bin/python" -m pip install -r "$requirements"
    cp "$requirements" "$inspection_env/.requirements"
fi
exec "$inspection_env/bin/python" "$project_dir/scripts/rerun_pad_measurements.py" "$@"
