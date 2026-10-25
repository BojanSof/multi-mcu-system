"""Release/archive/resource regressions use synthetic artifacts, never hardware."""

import json
import pathlib
import stat
import struct
import subprocess
import sys
import warnings
import zipfile

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
from firmware import release_archive, resource_report
from firmware.mmfw import PackageError
from test_phase11_tools import bundle  # Reuse ephemeral signing-key fixture only.


@pytest.fixture
def archive(bundle):
    manifest, public = bundle
    output = manifest.parent / "release.zip"
    release_archive.create(manifest, output, public)
    return output, public


def rewrite_archive(path, transform, compression=zipfile.ZIP_STORED):
    with zipfile.ZipFile(path) as archive:
        entries = [(info, archive.read(info)) for info in archive.infolist()]
    entries = transform(entries)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        with zipfile.ZipFile(path, "w", compression=compression) as archive:
            for info, data in entries:
                info.compress_type = compression
                archive.writestr(info, data)


def test_deterministic_archive_only_expected_artifacts(bundle, tmp_path):
    manifest, public = bundle
    first, second = tmp_path / "one.zip", tmp_path / "two.zip"
    before = manifest.read_bytes()
    release_archive.create(manifest, first, public)
    release_archive.create(manifest, second, public)
    assert first.read_bytes() == second.read_bytes()
    assert manifest.read_bytes() == before
    result = release_archive.verify(first, public)
    assert result["verified"] and result["system_version"] == "0.11.0"
    with zipfile.ZipFile(first) as archive:
        assert archive.namelist() == sorted(["release.json", "loader.bin", "nordic.bin",
                                             "slot0.mmfw", "slot1.mmfw"])
        for info in archive.infolist():
            assert info.date_time == release_archive.TIMESTAMP
            assert info.compress_type == zipfile.ZIP_STORED
            assert info.external_attr >> 16 == stat.S_IFREG | 0o644
        assert archive.read("release.json").endswith(b"\n")


def test_existing_output_no_clobber_and_explicit_force(bundle, tmp_path):
    manifest, public = bundle
    output = tmp_path / "release.zip"
    output.write_bytes(b"user file")
    with pytest.raises(FileExistsError):
        release_archive.create(manifest, output, public)
    assert output.read_bytes() == b"user file"
    release_archive.create(manifest, output, public, force=True)
    assert release_archive.verify(output, public)["verified"]
    assert not list(tmp_path.glob(".release-*.zip"))


def test_output_cannot_replace_input_or_symlink(bundle, tmp_path):
    manifest, public = bundle
    for output in (manifest, tmp_path / "loader.bin"):
        before = output.read_bytes()
        with pytest.raises(PackageError, match="source"):
            release_archive.create(manifest, output, public, force=True)
        assert output.read_bytes() == before
    link = tmp_path / "link.zip"
    link.symlink_to(manifest)
    with pytest.raises(PackageError):
        release_archive.create(manifest, link, public, force=True)


def test_creation_rejects_even_inside_bundle_symlinks(bundle, tmp_path):
    manifest, public = bundle
    loader = tmp_path / "loader.bin"
    original = tmp_path / "original.bin"
    loader.rename(original)
    loader.symlink_to(original)
    with pytest.raises(PackageError, match="symlink"):
        release_archive.create(manifest, tmp_path / "release.zip", public)


@pytest.mark.parametrize("damage", ["traversal", "absolute", "windows", "backslash", "symlink",
                                    "special", "directory", "extra", "missing", "duplicate",
                                    "unexpected", "hash", "oversized", "compression", "json",
                                    "json_duplicate", "artifact_duplicate", "reserved"])
def test_reject_unsafe_or_damaged_archives(archive, damage):
    path, public = archive
    def change(entries):
        if damage == "extra":
            return entries + [(zipfile.ZipInfo("private.pem"), b"not included")]
        if damage == "missing":
            return entries[:-1]
        if damage == "duplicate":
            return entries + [entries[0]]
        if damage == "compression":
            return entries
        index = next(i for i, (info, _) in enumerate(entries) if info.filename == "loader.bin")
        info, data = entries[index]
        if damage in ("traversal", "absolute", "windows", "backslash", "unexpected"):
            info = zipfile.ZipInfo({"traversal": "../loader.bin", "absolute": "/loader.bin",
                                    "windows": "C:/loader.bin", "backslash": "foo\\loader.bin",
                                    "unexpected": "not-expected.bin"}[damage])
        elif damage == "symlink":
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
        elif damage == "special":
            info.external_attr = (stat.S_IFIFO | 0o600) << 16
        elif damage == "directory":
            info = zipfile.ZipInfo("loader.bin/")
        elif damage == "hash":
            data = data[:-1] + b"X"
        elif damage == "oversized":
            data = bytes(release_archive.MAX_ARTIFACT + 1)
        else:
            index = next(i for i, (info, _) in enumerate(entries) if info.filename == "release.json")
            info, data = entries[index]
            if damage == "json":
                data = b"not json"
            elif damage == "json_duplicate":
                data = data.replace(b'"api": 1', b'"api": 1, "api": 1')
            else:
                document = json.loads(data)
                document["slots"][0]["file"] = ("loader.bin" if damage == "artifact_duplicate"
                                                 else "release.json")
                data = json.dumps(document).encode()
        entries[index] = info, data
        return entries
    rewrite_archive(path, change, zipfile.ZIP_DEFLATED if damage == "compression" else zipfile.ZIP_STORED)
    with pytest.raises((PackageError, ValueError, zipfile.BadZipFile)):
        release_archive.verify(path, public)


def test_reject_encrypted_flag_before_read(archive):
    path, public = archive
    data = bytearray(path.read_bytes())
    offset = data.index(b"PK\x01\x02")
    struct.pack_into("<H", data, offset + 8, 1)
    path.write_bytes(data)
    with pytest.raises(PackageError, match="encrypted"):
        release_archive.verify(path, public)


def test_archive_cli_and_invalid_source(bundle, archive, tmp_path, capsys):
    path, public = archive
    assert release_archive.main(["verify", str(path), "--public-key", str(public)]) == 0
    assert '"verified": true' in capsys.readouterr().out
    manifest, _ = bundle
    (tmp_path / "slot0.mmfw").write_bytes(b"damaged")
    with pytest.raises(PackageError, match="hash"):
        release_archive.create(manifest, tmp_path / "broken.zip", public)
    assert not (tmp_path / "broken.zip").exists()
    with pytest.raises(SystemExit):
        release_archive.main(["verify", str(tmp_path / "missing"), "--public-key", str(public)])


def test_size_parser_and_stack_parser(tmp_path):
    result = resource_report.parse_size(" text data bss dec hex filename\n 100 20 30 150 96 my image.elf\n")
    assert result == {"text": 100, "data": 20, "bss": 30, "flash_bytes": 120, "ram_bytes": 50}
    path = tmp_path / "file.su"
    path.write_text("/code/source.c:4:3:Module_Call\t128\tstatic\n/code/main.c:9:1:main\t256\tdynamic,bounded\n")
    frames = resource_report.parse_stack(path)
    assert frames[0]["function"] == "Module_Call" and frames[0]["frame_bytes"] == 128
    assert frames[1]["kind"] == "dynamic,bounded"


@pytest.mark.parametrize("output", ["", "unexpected", "1 2 3 5 5 file", "1 2 3 6 5 file",
                                    "1 2 3 6 6 file\n1 2 3 6 6 other"])
def test_bad_size_output(output):
    with pytest.raises(ValueError):
        resource_report.parse_size(output)


@pytest.mark.parametrize("content", ["bad", "file.c:1:2:f\t-1\tstatic",
                                     "file.c:1:2:f\t12\tunknown", "no-location\t12\tstatic"])
def test_bad_stack_output(tmp_path, content):
    path = tmp_path / "file.su"
    path.write_text(content)
    with pytest.raises(ValueError):
        resource_report.parse_stack(path)


def fake_size_tool(tmp_path, output="100 20 30 150 96 file.elf", exit_code=0):
    path = tmp_path / "fake-size"
    path.write_text(f"#!{sys.executable}\nprint({output!r})\nraise SystemExit({exit_code})\n")
    path.chmod(0o755)
    return path


def test_actual_tool_invocation_resource_cli_and_static_labels(tmp_path):
    elf = tmp_path / "app.elf"
    elf.write_bytes(b"\x7fELF" + bytes(12))
    tool = fake_size_tool(tmp_path)
    stack = tmp_path / "stack"
    stack.mkdir()
    (stack / "app.su").write_text("src.c:4:5:Module_Call\t64\tstatic\n")
    output = tmp_path / "report.json"
    assert resource_report.main(["--elf", f"app={elf}", "--size-tool", str(tool),
                                 "--stack-dir", str(stack), "--output", str(output)]) == 0
    document = json.loads(output.read_text())
    assert document["mode"] == "static_build_artifacts"
    assert document["elfs"][0]["flash_bytes"] == 120
    assert document["hardware_timings"] == "NOT_RUN_HARDWARE"
    assert "not call-chain" in document["stack_note"]
    assert document["stack_frames"][0]["frame_bytes"] == 64


def test_resource_missing_inputs_duplicate_names_tool_errors(tmp_path):
    elf = tmp_path / "app.elf"
    elf.write_bytes(b"\x7fELF")
    with pytest.raises(ValueError):
        resource_report.report([])
    with pytest.raises(ValueError):
        resource_report.report([("app", elf), ("app", elf)])
    with pytest.raises(ValueError):
        resource_report.report([("app", tmp_path / "absent")])
    with pytest.raises(subprocess.CalledProcessError):
        resource_report.report([("app", elf)], fake_size_tool(tmp_path, exit_code=2))
    with pytest.raises(ValueError, match="stack directory"):
        resource_report.report([("app", elf)], fake_size_tool(tmp_path), tmp_path / "absent")
    with pytest.raises(ValueError, match="no .su"):
        resource_report.report([("app", elf)], fake_size_tool(tmp_path), tmp_path)
    with pytest.raises(SystemExit):
        resource_report.main(["--elf", "bad-format"])


def test_resource_report_never_overwrites_elf_or_symlink(tmp_path):
    elf = tmp_path / "app.elf"
    original = b"\x7fELF" + bytes(12)
    elf.write_bytes(original)
    tool = fake_size_tool(tmp_path)
    with pytest.raises(SystemExit):
        resource_report.main(["--elf", f"app={elf}", "--size-tool", str(tool),
                              "--output", str(elf)])
    assert elf.read_bytes() == original
    link = tmp_path / "report.json"
    link.symlink_to(elf)
    with pytest.raises(SystemExit):
        resource_report.main(["--elf", f"app={elf}", "--size-tool", str(tool),
                              "--output", str(link)])
    assert elf.read_bytes() == original
