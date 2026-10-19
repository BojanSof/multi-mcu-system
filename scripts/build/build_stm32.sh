#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
selection=all
public_key_header=${BOOTLOADER_PUBLIC_KEY_HEADER:-}

usage() {
    cat <<'EOF'
Usage: scripts/build/build_stm32.sh [--profile PROFILE] [--public-key-header PATH]

Profiles:
  all          Normal release application, Slot 0, Slot 1, and bootloader (default)
  application  Normal release application only
  slots        Slot 0 and Slot 1 applications only
  bootloader   Bootloader only

The bootloader requires an external generated public-key header. Set
BOOTLOADER_PUBLIC_KEY_HEADER or pass --public-key-header. This script only builds; it never flashes.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --profile)
            [ "$#" -ge 2 ] || { echo "error: --profile needs a value" >&2; exit 2; }
            selection=$2
            shift 2
            ;;
        --public-key-header)
            [ "$#" -ge 2 ] || { echo "error: --public-key-header needs a value" >&2; exit 2; }
            public_key_header=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

case "$selection" in
    all|application|slots|bootloader) ;;
    *) echo "error: unknown profile: $selection" >&2; exit 2 ;;
esac

command -v cmake >/dev/null 2>&1 || { echo "error: cmake not found" >&2; exit 1; }
command -v arm-none-eabi-gcc >/dev/null 2>&1 || {
    echo "error: arm-none-eabi-gcc not found; source ./.env.local first" >&2
    exit 1
}

for dependency in \
    "$repo_root/third_party/stm32-cmake/cmake/stm32_gcc.cmake" \
    "$repo_root/.deps/cmsis_core/Core/Include" \
    "$repo_root/.deps/cmsis-device-u0/Include" \
    "$repo_root/.deps/stm32u0xx-hal-driver/Inc"; do
    if [ ! -e "$dependency" ]; then
        echo "error: missing STM32 dependency: $dependency" >&2
        echo "Run scripts/doctor.py and follow firmware/stm32u083/application/README.md." >&2
        exit 1
    fi
done

build_application_preset() {
    preset=$1
    cmake --preset "$preset" -S "$repo_root/firmware/stm32u083/application"
    cmake --build "$repo_root/firmware/stm32u083/application/build/$preset" --parallel
}

if [ "$selection" = all ] || [ "$selection" = application ]; then
    [ -d "$repo_root/.deps/FreeRTOS-Kernel" ] || {
        echo "error: missing STM32 dependency: $repo_root/.deps/FreeRTOS-Kernel" >&2
        exit 1
    }
    build_application_preset release-personality-b
fi
if [ "$selection" = all ] || [ "$selection" = slots ]; then
    [ -d "$repo_root/.deps/FreeRTOS-Kernel" ] || {
        echo "error: missing STM32 dependency: $repo_root/.deps/FreeRTOS-Kernel" >&2
        exit 1
    }
    build_application_preset release-slot0
    build_application_preset release-slot1
fi

if [ "$selection" = all ] || [ "$selection" = bootloader ]; then
    [ -n "$public_key_header" ] || {
        echo "error: bootloader build requires --public-key-header PATH" >&2
        exit 1
    }
    case "$public_key_header" in
        /*) ;;
        *) public_key_header="$repo_root/$public_key_header" ;;
    esac
    [ -f "$public_key_header" ] || {
        echo "error: public-key header not found: $public_key_header" >&2
        exit 1
    }
    [ -f "$repo_root/third_party/micro-ecc/uECC.c" ] || {
        echo "error: missing micro-ecc submodule; run git submodule update --init --recursive" >&2
        exit 1
    }
    [ -f "$repo_root/third_party/mbedtls/library/sha256.c" ] || {
        echo "error: missing Mbed TLS submodule; run git submodule update --init --recursive" >&2
        exit 1
    }
    cmake --preset release -S "$repo_root/firmware/stm32u083/bootloader" \
        -DBOOTLOADER_PUBLIC_KEY_HEADER="$public_key_header"
    cmake --build "$repo_root/firmware/stm32u083/bootloader/build/release" --parallel
fi
