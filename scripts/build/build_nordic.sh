#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
profile=default
stm_image=

usage() {
    cat <<'EOF'
Usage: scripts/build/build_nordic.sh [--profile PROFILE] [--stm-image PATH]

Profiles:
  default        Normal host firmware (default)
  phase6         MCUboot/MCUmgr firmware
  phase9         MCUboot/MCUmgr host control API for scripts/mmctl
  phase10        Host control plus streamed STM32 A/B update and remote reset
  phase11        Phase 10 plus factory-ROM bootloader repair
  phase12        Final metrics/operator profile (warning/error logs)
  phase12-verbose Same metrics profile with informational logs for comparison
  phase7-query   STM bootloader query client
  phase7-update  STM bootloader update client; requires --stm-image

All output is placed below root build/. This script only builds; it never flashes.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --profile)
            [ "$#" -ge 2 ] || { echo "error: --profile needs a value" >&2; exit 2; }
            profile=$2
            shift 2
            ;;
        --stm-image)
            [ "$#" -ge 2 ] || { echo "error: --stm-image needs a value" >&2; exit 2; }
            stm_image=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *) echo "error: unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

case "$profile" in
    default|phase6|phase9|phase10|phase11|phase12|phase12-verbose|phase7-query|phase7-update) ;;
    *) echo "error: unknown profile: $profile" >&2; exit 2 ;;
esac

west_command=${WEST:-}
if [ -z "$west_command" ]; then
    if [ -x "$repo_root/.venv/bin/west" ]; then
        west_command="$repo_root/.venv/bin/west"
    else
        west_command=west
    fi
fi
command -v "$west_command" >/dev/null 2>&1 || {
    echo "error: west not found; run scripts/bootstrap-venv.sh" >&2
    exit 1
}
[ -f "$repo_root/.ncs-workspace/nrf/VERSION" ] || {
    echo "error: NCS workspace not found at $repo_root/.ncs-workspace" >&2
    exit 1
}

board=nrf54l15dk/nrf54l15/cpuapp
source_dir="$repo_root/firmware/nrf54l15-host"

case "$profile" in
    default)
        build_dir="$repo_root/build/nrf54l15-host"
        "$west_command" build -p always --no-sysbuild -b "$board" "$source_dir" \
            --build-dir "$build_dir"
        ;;
    phase6)
        build_dir="$repo_root/build/nrf54l15-host-phase6"
        "$west_command" build -p always -b "$board" "$source_dir" \
            --build-dir "$build_dir" -- \
            -DEXTRA_CONF_FILE=phase6_mcumgr.conf \
            -DEXTRA_DTC_OVERLAY_FILE=boards/nrf54l15dk_nrf54l15_cpuapp_phase6_mcumgr.overlay \
            -DSB_EXTRA_CONF_FILE="$source_dir/sysbuild_phase6_mcumgr.conf"
        ;;
    phase9)
        build_dir="$repo_root/build/nrf54l15-host-phase9"
        "$west_command" build -p always -b "$board" "$source_dir" \
            --build-dir "$build_dir" -- \
            -Dnrf54l15-host_MULTI_MCU_PHASE9_CONTROL=ON \
            '-DEXTRA_CONF_FILE=phase6_mcumgr.conf;phase9_control.conf' \
            -DEXTRA_DTC_OVERLAY_FILE=boards/nrf54l15dk_nrf54l15_cpuapp_phase6_mcumgr.overlay \
            -DSB_EXTRA_CONF_FILE="$source_dir/sysbuild_phase6_mcumgr.conf"
        ;;
    phase10)
        build_dir="$repo_root/build/nrf54l15-host-phase10"
        "$west_command" build -p always -b "$board" "$source_dir" \
            --build-dir "$build_dir" -- \
            -Dnrf54l15-host_MULTI_MCU_PHASE9_CONTROL=ON \
            -Dnrf54l15-host_MULTI_MCU_PHASE10_UPDATE=ON \
            '-DEXTRA_CONF_FILE=phase6_mcumgr.conf;phase9_control.conf;phase10_update.conf' \
            '-DEXTRA_DTC_OVERLAY_FILE=boards/nrf54l15dk_nrf54l15_cpuapp_phase6_mcumgr.overlay;boards/nrf54l15dk_nrf54l15_cpuapp_phase10_update.overlay' \
            -DSB_EXTRA_CONF_FILE="$source_dir/sysbuild_phase6_mcumgr.conf"
        ;;
    phase11)
        build_dir="$repo_root/build/nrf54l15-host-phase11"
        "$west_command" build -p always -b "$board" "$source_dir" \
            --build-dir "$build_dir" -- \
            -Dnrf54l15-host_MULTI_MCU_PHASE9_CONTROL=ON \
            -Dnrf54l15-host_MULTI_MCU_PHASE10_UPDATE=ON \
            -Dnrf54l15-host_MULTI_MCU_PHASE11_ROM=ON \
            '-DEXTRA_CONF_FILE=phase6_mcumgr.conf;phase9_control.conf;phase10_update.conf;phase11_rom.conf' \
            '-DEXTRA_DTC_OVERLAY_FILE=boards/nrf54l15dk_nrf54l15_cpuapp_phase6_mcumgr.overlay;boards/nrf54l15dk_nrf54l15_cpuapp_phase10_update.overlay' \
            -DSB_EXTRA_CONF_FILE="$source_dir/sysbuild_phase6_mcumgr.conf"
        ;;
    phase12|phase12-verbose)
        build_dir="$repo_root/build/nrf54l15-host-$profile"
        log_config=phase12_optimized.conf
        [ "$profile" != phase12-verbose ] || log_config=phase12_verbose.conf
        "$west_command" build -p always -b "$board" "$source_dir" \
            --build-dir "$build_dir" -- \
            -Dnrf54l15-host_MULTI_MCU_PHASE9_CONTROL=ON \
            -Dnrf54l15-host_MULTI_MCU_PHASE10_UPDATE=ON \
            -Dnrf54l15-host_MULTI_MCU_PHASE11_ROM=ON \
            -Dnrf54l15-host_MULTI_MCU_PHASE12_METRICS=ON \
            "-DEXTRA_CONF_FILE=phase6_mcumgr.conf;phase9_control.conf;phase10_update.conf;phase11_rom.conf;phase12_metrics.conf;$log_config" \
            '-DEXTRA_DTC_OVERLAY_FILE=boards/nrf54l15dk_nrf54l15_cpuapp_phase6_mcumgr.overlay;boards/nrf54l15dk_nrf54l15_cpuapp_phase10_update.overlay' \
            -DSB_EXTRA_CONF_FILE="$source_dir/sysbuild_phase6_mcumgr.conf"
        ;;
    phase7-query)
        build_dir="$repo_root/build/nrf54l15-host-phase7-query"
        "$west_command" build -p always --no-sysbuild -b "$board" "$source_dir" \
            --build-dir "$build_dir" -- -DMULTI_MCU_PHASE7_STM_BOOTLOADER_DEMO=ON
        ;;
    phase7-update)
        [ -n "$stm_image" ] || {
            echo "error: phase7-update requires --stm-image PATH" >&2
            exit 1
        }
        case "$stm_image" in
            /*) ;;
            *) stm_image="$repo_root/$stm_image" ;;
        esac
        [ -f "$stm_image" ] || { echo "error: STM image not found: $stm_image" >&2; exit 1; }
        build_dir="$repo_root/build/nrf54l15-host-phase7-update"
        "$west_command" build -p always --no-sysbuild -b "$board" "$source_dir" \
            --build-dir "$build_dir" -- \
            -DMULTI_MCU_PHASE7_STM_BOOTLOADER_DEMO=ON \
            -DMULTI_MCU_PHASE7_STM_IMAGE="$stm_image"
        ;;
esac

if [ "$profile" = phase9 ] || [ "$profile" = phase10 ] || [ "$profile" = phase11 ] ||
   [ "$profile" = phase12 ] || [ "$profile" = phase12-verbose ]; then
    application_dir="$build_dir/nrf54l15-host"
    if ! grep -q '^MULTI_MCU_PHASE9_CONTROL:BOOL=ON$' "$application_dir/CMakeCache.txt"; then
        echo "error: Phase 9 control was not enabled in the application image" >&2
        exit 1
    fi
    if ! grep -q 'HostControl.c.obj' "$application_dir/zephyr/zephyr.map"; then
        echo "error: Phase 9 control handler is missing from the linked firmware" >&2
        exit 1
    fi
fi

if [ "$profile" = phase10 ] || [ "$profile" = phase11 ] ||
   [ "$profile" = phase12 ] || [ "$profile" = phase12-verbose ]; then
    if ! grep -q '^MULTI_MCU_PHASE10_UPDATE:BOOL=ON$' "$application_dir/CMakeCache.txt"; then
        echo "error: Phase 10 update was not enabled in the application image" >&2
        exit 1
    fi
    for object in HostUpdateAdapter HostStmUpdate; do
        if ! grep -q "$object.c.obj" "$application_dir/zephyr/zephyr.map"; then
            echo "error: Phase 10 $object is missing from linked firmware" >&2
            exit 1
        fi
    done
fi

if [ "$profile" = phase11 ] || [ "$profile" = phase12 ] || [ "$profile" = phase12-verbose ]; then
    if ! grep -q '^MULTI_MCU_PHASE11_ROM:BOOL=ON$' "$application_dir/CMakeCache.txt"; then
        echo "error: Phase 11 ROM recovery was not enabled in the application image" >&2
        exit 1
    fi
    if ! grep -q 'Stm32RomBoot.c.obj' "$application_dir/zephyr/zephyr.map"; then
        echo "error: Phase 11 ROM library is missing from linked firmware" >&2
        exit 1
    fi
fi

if [ "$profile" = phase12 ] || [ "$profile" = phase12-verbose ]; then
    if ! grep -q '^MULTI_MCU_PHASE12_METRICS:BOOL=ON$' "$application_dir/CMakeCache.txt" ||
       ! grep -q 'HostControl_MetricsRead' "$application_dir/zephyr/zephyr.map"; then
        echo "error: Phase 12 metrics are missing from linked application" >&2
        exit 1
    fi
    log_level=2
    [ "$profile" != phase12-verbose ] || log_level=3
    if ! grep -q "^CONFIG_LOG_MAX_LEVEL=$log_level$" "$application_dir/zephyr/.config"; then
        echo "error: Phase 12 log level does not match the requested profile" >&2
        exit 1
    fi
fi

echo "Built Nordic profile '$profile' in $build_dir"
