#!/usr/bin/env python3
"""Create or verify a deterministic bounded release-v2 ZIP; never access hardware."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import stat
import tempfile
import zipfile

try:
    from . import release_bundle
    from .mmfw import PackageError, deterministic_json
except ImportError:
    import release_bundle
    from mmfw import PackageError, deterministic_json

MANIFEST = "release.json"
MAX_ARTIFACT = 2 * 1024 * 1024
MAX_MANIFEST = 65536
MAX_ARCHIVE = MAX_ARTIFACT * 4 + MAX_MANIFEST + 65536
TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def _path(name):
    if (type(name) is not str or not name or "\\" in name or "\x00" in name or
            pathlib.PurePosixPath(name).is_absolute() or pathlib.PureWindowsPath(name).drive or
            any(part in ("", ".", "..") for part in name.split("/"))):
        raise PackageError("unsafe archive path")
    return pathlib.PurePosixPath(name)


def _records(document):
    # Full schema verification follows in release_bundle.validate. Validate just
    # enough here to decide the exact bounded members before temporary writes.
    if type(document) is not dict:
        raise PackageError("manifest must be an object")
    try:
        records = [document["nordic"], document["bootloader"], *document["slots"]]
        if len(records) != 4:
            raise PackageError("four release artifacts required")
        names = [record["file"] for record in records]
    except (KeyError, TypeError) as error:
        raise PackageError("missing release artifacts") from error
    for name in names:
        _path(name)
    if MANIFEST in names or len(set(names)) != 4:
        raise PackageError("duplicate or reserved artifact name")
    return records


def _source_data(root, record):
    path = root
    for part in _path(record["file"]).parts:
        path = path / part
        if path.is_symlink():
            raise PackageError("source artifact symlinks are not allowed")
    return release_bundle._artifact(root, record)


def create(manifest, output, public_key, key_id=1, force=False):
    manifest = pathlib.Path(manifest).resolve()
    output = pathlib.Path(output).absolute()
    bundle = release_bundle.load(manifest, public_key, key_id)
    records = _records(bundle.manifest)
    canonical = deterministic_json(bundle.manifest).encode("utf-8")
    if len(canonical) > MAX_MANIFEST:
        raise PackageError("manifest too large")
    files = {MANIFEST: canonical}
    for record in records:
        files[record["file"]] = _source_data(manifest.parent, record)
    # Validate the exact snapshot that will be archived, closing the gap between
    # initial file validation and collection if a source changed in the meantime.
    with tempfile.TemporaryDirectory(prefix="multi-mcu-release-check-") as directory:
        root = pathlib.Path(directory)
        for name, data in files.items():
            path = root.joinpath(*_path(name).parts)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        release_bundle.validate(bundle.manifest, root, public_key, key_id)
    inputs = {manifest, *(manifest.parent / record["file"] for record in records)}
    if output.resolve() in inputs or output.is_symlink():
        raise PackageError("archive output must not overwrite a source or symlink")
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".release-", suffix=".zip", dir=output.parent)
    os.close(descriptor)
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_STORED) as archive:
            for name in sorted(files):
                info = zipfile.ZipInfo(name, TIMESTAMP)
                info.create_system = 3
                info.external_attr = (stat.S_IFREG | 0o644) << 16
                info.compress_type = zipfile.ZIP_STORED
                archive.writestr(info, files[name])
        if force:
            os.replace(temporary, output)
        else:
            # Atomic no-clobber publication, including a competing file creator.
            os.link(temporary, output)
        return {"verified": True, "system_version": bundle.manifest["system_version"],
                "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}
    finally:
        pathlib.Path(temporary).unlink(missing_ok=True)


def verify(path, public_key, key_id=1):
    path = pathlib.Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_ARCHIVE:
        raise PackageError("archive missing or exceeds bounded size")
    with zipfile.ZipFile(path) as archive:
        members = archive.infolist()
        if len(members) != 5:
            raise PackageError("archive must contain manifest and exactly four artifacts")
        names = set()
        for member in members:
            _path(member.filename)
            mode = member.external_attr >> 16
            if (member.orig_filename != member.filename or member.filename in names or
                    member.is_dir() or stat.S_ISLNK(mode) or
                    stat.S_IFMT(mode) not in (0, stat.S_IFREG) or member.flag_bits & 1 or
                    member.compress_type != zipfile.ZIP_STORED or
                    member.file_size != member.compress_size or
                    member.file_size > (MAX_MANIFEST if member.filename == MANIFEST else MAX_ARTIFACT)):
                raise PackageError("unsafe, duplicate, encrypted or oversized archive member")
            names.add(member.filename)
        if MANIFEST not in names:
            raise PackageError("archive manifest missing")
        document = json.loads(archive.read(MANIFEST), object_pairs_hook=release_bundle._unique)
        records = _records(document)
        if names != {MANIFEST, *(record["file"] for record in records)}:
            raise PackageError("unexpected or missing archive artifact")
        with tempfile.TemporaryDirectory(prefix="multi-mcu-release-verify-") as directory:
            root = pathlib.Path(directory)
            for record in records:
                name = record["file"]
                destination = root.joinpath(*_path(name).parts)
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(archive.read(name))
            release_bundle.validate(document, root, public_key, key_id)
    return {"verified": True, "system_version": document["system_version"],
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("create")
    build.add_argument("manifest", type=pathlib.Path)
    build.add_argument("output", type=pathlib.Path)
    build.add_argument("--force", action="store_true")
    check = commands.add_parser("verify")
    check.add_argument("archive", type=pathlib.Path)
    for command in (build, check):
        command.add_argument("--public-key", required=True, type=pathlib.Path)
        command.add_argument("--key-id", type=lambda value: int(value, 0), default=1)
    args = parser.parse_args(argv)
    try:
        if args.command == "create":
            result = create(args.manifest, args.output, args.public_key, args.key_id, args.force)
        else:
            result = verify(args.archive, args.public_key, args.key_id)
        print(deterministic_json(result), end="")
        return 0
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
