#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
python_command=${PYTHON:-}
cd "$repo_root"

if [ -z "$python_command" ]; then
    if [ -x "$repo_root/.venv/bin/python" ]; then
        python_command="$repo_root/.venv/bin/python"
    else
        python_command=python3
    fi
fi

for command in cmake ctest "$python_command"; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "error: required command not found: $command" >&2
        exit 1
    fi
done

echo "==> Configuring native libraries"
cmake --preset native-debug -S "$repo_root"
echo "==> Building native libraries"
cmake --build --preset native-debug --parallel
echo "==> Running native library tests"
ctest --preset native-debug --test-dir "$repo_root/build/native-debug"

echo "==> Configuring bootloader state-machine tests"
cmake -S "$repo_root/firmware/stm32u083/bootloader" \
    -B "$repo_root/build/stm32-bootloader-native" -G Ninja \
    -DBOOTLOADER_BUILD_NATIVE_TESTS=ON
echo "==> Building bootloader state-machine tests"
cmake --build "$repo_root/build/stm32-bootloader-native" --parallel
echo "==> Running bootloader state-machine tests"
ctest --test-dir "$repo_root/build/stm32-bootloader-native" --output-on-failure

echo "==> Checking Python dependencies"
"$python_command" -m pip check
echo "==> Running Python tool tests"
"$python_command" -m pytest "$repo_root/tools/tests" "$repo_root/tests/hil"
