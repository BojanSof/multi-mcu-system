#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
venv_path="$repo_root/.venv"
lock_path="$repo_root/tools/requirements.lock"
python_command=${PYTHON:-python3}

if ! command -v "$python_command" >/dev/null 2>&1; then
    echo "error: Python 3 was not found. Install Python 3.10 or newer and rerun." >&2
    exit 1
fi

if ! "$python_command" -c 'import sys; raise SystemExit(sys.version_info < (3, 10))'; then
    echo "error: Python 3.10 or newer is required." >&2
    "$python_command" --version >&2 || true
    exit 1
fi

if [ ! -f "$lock_path" ]; then
    echo "error: missing pinned dependency file: $lock_path" >&2
    exit 1
fi

if [ ! -x "$venv_path/bin/python" ]; then
    echo "Creating project virtual environment at $venv_path"
    "$python_command" -m venv "$venv_path" || {
        echo "error: venv creation failed. Install your distribution's Python venv package." >&2
        exit 1
    }
fi

if ! "$venv_path/bin/python" -m pip install --upgrade pip; then
    echo "error: pip upgrade failed. Check network/proxy access and retry; the venv was left intact." >&2
    exit 1
fi
if ! "$venv_path/bin/python" -m pip install --requirement "$lock_path"; then
    echo "error: pinned dependency installation failed. Check network/proxy access and retry." >&2
    exit 1
fi

echo "Virtual environment ready: $venv_path"
echo "Run tools explicitly with: $venv_path/bin/python"
