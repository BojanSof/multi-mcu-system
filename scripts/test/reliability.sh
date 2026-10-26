#!/bin/sh
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [ ! -x "$repo_root/.venv/bin/python" ]; then
    echo "Missing project .venv; run scripts/bootstrap-venv.sh" >&2
    exit 1
fi
exec "$repo_root/.venv/bin/python" "$repo_root/tests/hil/reliability_suite.py" "$@"
