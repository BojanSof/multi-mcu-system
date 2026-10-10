#!/usr/bin/env python3
"""Audit the Multi-MCU Lab development environment without third-party modules."""

from __future__ import annotations

import argparse
import glob
import grp
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from typing import Sequence


ROOT = pathlib.Path(__file__).resolve().parents[1]
LOCAL_VENV = ROOT / ".venv" / "bin"
LOCAL_ARM_BIN = pathlib.Path(
    "/home/bojan/toolchains/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin"
)
LOCAL_CUBE_PROGRAMMER_BIN = pathlib.Path(
    "/home/bojan/st/stm32cubeclt_1.22.0/STM32CubeProgrammer/bin"
)


@dataclass(frozen=True)
class Check:
    name: str
    required_for: str
    status: str
    version: str
    path: str
    action: str


def run(command: Sequence[str], timeout: float = 3.0) -> tuple[int, str]:
    try:
        result = subprocess.run(
            command,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired):
        return 1, ""
    return result.returncode, result.stdout.strip()


def first_line(value: str) -> str:
    ansi_escape = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
    for line in value.splitlines():
        cleaned = ansi_escape.sub("", line).strip()
        if cleaned and set(cleaned) != {"-"}:
            return cleaned
    return "unknown"


def install_hint(packages: str, url: str) -> str:
    system = platform.system()
    os_release = {}
    try:
        for line in pathlib.Path("/etc/os-release").read_text(encoding="utf-8").splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                os_release[key] = value.strip('"')
    except OSError:
        pass
    distro = os_release.get("ID", "")
    if distro in {"arch", "manjaro", "endeavouros"}:
        return f"Install with: pacman -S --needed {packages} (run yourself with appropriate privileges)."
    if distro in {"ubuntu", "debian", "linuxmint", "pop"}:
        return f"Install with: apt install {packages} (run yourself with appropriate privileges)."
    if distro in {"fedora", "rhel", "centos"}:
        return f"Install with: dnf install {packages} (run yourself with appropriate privileges)."
    if system == "Darwin":
        return f"Install using Homebrew or the vendor package. See {url}"
    return f"Install from the platform package manager or official source: {url}"


def tool_check(
    name: str,
    commands: Sequence[str],
    version_args: Sequence[str],
    required_for: str,
    package: str,
    url: str,
    version_pattern: str | None = None,
    action_override: str | None = None,
) -> Check:
    path = next((shutil.which(command) for command in commands if shutil.which(command)), None)
    action = action_override or install_hint(package, url)
    if path is None:
        return Check(name, required_for, "MISSING", "-", "-", action)
    return_code, output = run([path, *version_args])
    version = first_line(output)
    if return_code != 0 and not (version_pattern and re.search(version_pattern, output, re.IGNORECASE)):
        return Check(name, required_for, "VERSION_MISMATCH", version, path, f"The executable was found but did not run successfully. {action}")
    if version_pattern and not re.search(version_pattern, output, re.IGNORECASE):
        return Check(name, required_for, "VERSION_MISMATCH", version, path, action)
    return Check(name, required_for, "READY", version, path, "None")


def directory_check(name: str, required_for: str, candidates: Sequence[pathlib.Path], action: str) -> Check:
    location = next((path for path in candidates if path.exists()), None)
    if location is None:
        return Check(name, required_for, "MISSING", "-", "-", action)
    return Check(name, required_for, "READY", "present", str(location), "None")


def ncs_check() -> Check:
    action = (
        "Install nRF Connect SDK v3.4.1 with the official Toolchain Manager or west instructions, "
        "enter its environment, and verify: west zephyr-export && west build -b "
        "nrf54l15dk/nrf54l15/cpuapp firmware/nrf54l15-host."
    )
    roots = []
    ncs_root = os.environ.get("NCS_ROOT")
    zephyr_base = os.environ.get("ZEPHYR_BASE")
    if ncs_root:
        roots.append(pathlib.Path(ncs_root))
    if zephyr_base:
        roots.append(pathlib.Path(zephyr_base).parent / "nrf")
    for root in roots:
        version_file = root / "VERSION"
        if not version_file.is_file():
            continue
        try:
            version_text = version_file.read_text(encoding="utf-8")
        except OSError:
            continue
        if re.search(r"VERSION_MAJOR\s*=\s*3", version_text) and re.search(
            r"VERSION_MINOR\s*=\s*4", version_text
        ) and re.search(r"PATCHLEVEL\s*=\s*1", version_text):
            return Check("nRF Connect SDK v3.4.1", "Phase 1 Nordic", "READY", "3.4.1", str(root), "None")
        return Check(
            "nRF Connect SDK v3.4.1",
            "Phase 1 Nordic",
            "VERSION_MISMATCH",
            first_line(version_text),
            str(root),
            action,
        )
    return Check("nRF Connect SDK v3.4.1", "Phase 1 Nordic", "MISSING", "-", "-", action)


def serial_checks() -> list[Check]:
    devices = sorted(set(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")))
    if not devices:
        return [Check(
            "USB serial/debug devices",
            "Phase 1 hardware",
            "MISSING",
            "no ttyACM/ttyUSB devices detected",
            "-",
            "Connect each board by USB, then rerun this script. Do not connect the boards to each other in Phase 1.",
        )]
    readable = [device for device in devices if os.access(device, os.R_OK | os.W_OK)]
    if len(readable) != len(devices):
        groups = ", ".join(group.gr_name for group in grp.getgrall() if os.getuid() in group.gr_mem)
        return [Check(
            "USB serial/debug devices",
            "Phase 1 hardware",
            "VERSION_MISMATCH",
            f"accessible {len(readable)}/{len(devices)}; supplementary groups: {groups or 'none'}",
            ", ".join(devices),
            "Install vendor udev rules or add your user to the distribution's serial-access group, log out/in, and verify read/write access; do not chmod devices globally.",
        )]
    return [Check("USB serial/debug devices", "Phase 1 hardware", "READY", f"{len(devices)} accessible", ", ".join(devices), "None")]


def board_checks() -> list[Check]:
    lsusb = shutil.which("lsusb")
    if lsusb is None:
        return []
    return_code, output = run([lsusb])
    if return_code != 0:
        return [Check("USB board enumeration", "Phase 1 hardware", "VERSION_MISMATCH", first_line(output), lsusb, "Resolve USB/libusb access, connect each board independently, and rerun the audit.")]
    lower = output.lower()
    definitions = (
        ("SEGGER USB probe (board identity requires nrfutil)", ("1366:", "segger")),
        ("ST-LINK USB probe (board identity requires target query)", ("0483:", "st-link")),
    )
    checks = []
    for name, markers in definitions:
        present = any(marker in lower for marker in markers)
        checks.append(Check(
            name,
            "Phase 1 hardware",
            "READY" if present else "MISSING",
            "enumerated" if present else "not detected",
            "USB" if present else "-",
            "None" if present else "Connect the board by its debugger USB connector, verify it is powered, and rerun the audit. Do not flash or interconnect boards yet.",
        ))
    return checks


def collect_checks() -> list[Check]:
    phase1_url = "https://cmake.org/download/"
    checks = [
        tool_check("git", ["git"], ["--version"], "Phase 1", "git", "https://git-scm.com/downloads"),
        tool_check("cmake", ["cmake"], ["--version"], "Phase 1", "cmake", phase1_url, r"cmake version (3\.(2[5-9]|[3-9][0-9])|[4-9]\.)"),
        tool_check("ctest", ["ctest"], ["--version"], "Phase 1", "cmake", phase1_url),
        tool_check("ninja", ["ninja"], ["--version"], "Phase 1", "ninja", "https://ninja-build.org/"),
        tool_check("native C compiler", ["cc", "gcc", "clang"], ["--version"], "Phase 1", "base-devel", "https://gcc.gnu.org/"),
        tool_check("python3", ["python3"], ["--version"], "Phase 1", "python python-pip", "https://www.python.org/downloads/", r"Python 3\.(1[0-9]|[2-9][0-9])"),
        tool_check("clang-format", ["clang-format"], ["--version"], "Phase 1+", "clang", "https://clang.llvm.org/docs/ClangFormat.html"),
        tool_check("clang-tidy (optional)", ["clang-tidy"], ["--version"], "Phase 1+ optional", "clang", "https://clang.llvm.org/extra/clang-tidy/"),
        tool_check("cppcheck (optional)", ["cppcheck"], ["--version"], "Phase 1+ optional", "cppcheck", "https://cppcheck.sourceforge.io/"),
        tool_check("Arm GNU compiler", ["arm-none-eabi-gcc", str(LOCAL_ARM_BIN / "arm-none-eabi-gcc")], ["--version"], "Phase 1 STM32", "arm-none-eabi-gcc arm-none-eabi-newlib", "https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads"),
        tool_check("Arm GNU binutils", ["arm-none-eabi-objcopy", str(LOCAL_ARM_BIN / "arm-none-eabi-objcopy")], ["--version"], "Phase 1 STM32", "arm-none-eabi-binutils", "https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads"),
        tool_check("west", ["west", str(LOCAL_VENV / "west")], ["--version"], "Phase 1 Nordic", "python-west", "https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/installation/install_ncs.html", action_override="Run scripts/bootstrap-venv.sh to install west 1.5.0 into the project-local .venv, then verify: .venv/bin/west --version"),
        tool_check("nrfutil", ["nrfutil"], ["--version"], "Phase 1 Nordic flash", "nrfutil", "https://www.nordicsemi.com/Products/Development-tools/nRF-Util", action_override="Install nRF Util using Nordic's official download, ensure its log directory is writable, then verify: nrfutil --version"),
        tool_check("J-Link", ["JLinkExe", "JLink"], ["-version"], "Phase 1 Nordic debug", "jlink-software-and-documentation", "https://www.segger.com/downloads/jlink/", r"J-Link Commander V[0-9]", "Install SEGGER J-Link Software from https://www.segger.com/downloads/jlink/ and verify: JLinkExe -version"),
        tool_check("STM32CubeProgrammer", ["STM32_Programmer_CLI", str(LOCAL_CUBE_PROGRAMMER_BIN / "STM32_Programmer_CLI")], ["--version"], "Phase 1 STM32 flash", "stm32cubeprogrammer", "https://www.st.com/en/development-tools/stm32cubeprog.html", action_override="Install STM32CubeCLT or STM32CubeProgrammer, then verify: STM32_Programmer_CLI --version"),
        tool_check("OpenOCD alternative", ["openocd"], ["--version"], "Phase 1 STM32 debug alternative", "openocd", "https://openocd.org/"),
        tool_check("lsusb", ["lsusb"], ["--version"], "Phase 1 hardware detection", "usbutils", "https://www.kernel.org/pub/linux/utils/usb/usbutils/"),
        tool_check("sigrok-cli (optional)", ["sigrok-cli"], ["--version"], "Phase 4 optional", "libsigrok", "https://sigrok.org/wiki/Downloads"),
    ]
    checks.extend([
        ncs_check(),
        directory_check(
            "ObKo stm32-cmake submodule",
            "Phase 1 STM32",
            [ROOT / "third_party" / "stm32-cmake" / "cmake" / "stm32_gcc.cmake"],
            "Initialize the pinned dependency with: git submodule update --init --recursive third_party/stm32-cmake",
        ),
    ])
    checks.extend(serial_checks())
    checks.extend(board_checks())
    return checks


def host_info() -> dict[str, str]:
    return {
        "os": platform.platform(),
        "architecture": platform.machine(),
        "python": platform.python_version(),
        "repository": str(ROOT),
    }


def print_markdown(checks: Sequence[Check]) -> None:
    info = host_info()
    print("# Multi-MCU Lab dependency audit")
    print(f"\n- OS: `{info['os']}`")
    print(f"- Architecture: `{info['architecture']}`")
    print(f"- Python: `{info['python']}`")
    print("\n| Tool | Required for | Status | Version | Path |")
    print("|---|---|---|---|---|")
    for check in checks:
        values = (check.name, check.required_for, check.status, check.version, check.path)
        print("| " + " | ".join(value.replace("|", "\\|") for value in values) + " |")
    outstanding = [check for check in checks if check.status != "READY"]
    print("\n## Required actions")
    if not outstanding:
        print("\nNone.")
    for check in outstanding:
        print(f"\n- **{check.name} ({check.status}, {check.required_for}):** {check.action}")
    print("\nHardware safety: do not interconnect the boards in Phase 1. Before later wiring, verify the exact board revisions and compatible I/O voltage; never connect board power rails.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="emit JSON rather than Markdown")
    parser.add_argument("--strict", action="store_true", help="return nonzero if any Phase 1 check is not ready")
    args = parser.parse_args()
    checks = collect_checks()
    if args.json:
        print(json.dumps({"host": host_info(), "checks": [asdict(check) for check in checks]}, indent=2))
    else:
        print_markdown(checks)
    if args.strict and any(
        check.status != "READY"
        and check.required_for.startswith("Phase 1")
        and "optional" not in check.required_for
        and "alternative" not in check.required_for
        for check in checks
    ):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
