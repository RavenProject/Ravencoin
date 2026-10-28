#!/usr/bin/env python3
"""Verify all unsigned Build Raven bundles from one workflow run and commit."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import posixpath
import re
import stat
import struct
import subprocess
import tarfile
import tempfile
import zipfile
import gzip
import shutil


# Target, GNU host, package suffixes, machine identifier, primary binary format.
TARGETS = {
    "windows": ("x86_64-w64-mingw32", ("-win64.zip", "-win64-setup-unsigned.exe", "-win64-unsigned.tar.gz"), 0x8664, "pe"),
    "osx": ("x86_64-apple-darwin14", ("-osx64.tar.gz", "-osx-unsigned.dmg", "-osx-unsigned.tar.gz"), 0x01000007, "macho"),
    "linux": ("x86_64-linux-gnu", ("-x86_64-linux-gnu.tar.gz",), 0x3E, "elf"),
    "arm32v7": ("arm-linux-gnueabihf", ("-arm-linux-gnueabihf.tar.gz",), 0x28, "elf"),
    "aarch64": ("aarch64-linux-gnu", ("-aarch64-linux-gnu.tar.gz",), 0xB7, "elf"),
}
STATUS = "unsigned CI build; Windows and macOS outputs are not signed final releases"
MIN_BINARY_SIZE = 64 * 1024
MIN_INSTALLER_SIZE = 1024 * 1024
HEADER_READ_SIZE = 256 * 1024
MAX_CONTAINER_MEMBER_SIZE = 128 * 1024 * 1024


def digest(path, algorithm="sha256"):
    hasher = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def archive_name(name, label):
    if (not name or name.startswith("/") or "\\" in name or "\0" in name
            or re.match(r"^[A-Za-z]:", name) or ".." in name.split("/")):
        raise ValueError(f"{label}: unsafe archive path {name!r}")
    normalized = posixpath.normpath(name)
    if normalized.startswith("../") or normalized == "..":
        raise ValueError(f"{label}: unsafe archive path {name!r}")
    return normalized


def inspect_tar(path, prefix=None, mac_signing_bundle=False):
    members = {}
    links = set()
    try:
        with tarfile.open(path, "r:gz") as archive:
            for member in archive:
                name = archive_name(member.name, path.name)
                if prefix is not None and name != prefix and not name.startswith(prefix + "/"):
                    raise ValueError(f"{path.name}: unsafe archive path {member.name!r}")
                if name in members:
                    raise ValueError(f"{path.name}: duplicate archive path {name!r}")
                members[name] = member
                if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
                    raise ValueError(f"{path.name}: unsupported archive entry {name!r}")
                if member.issym():
                    special = mac_signing_bundle and name == "dist/Applications" and member.linkname == "/Applications"
                    if not special:
                        target = archive_name(member.linkname, path.name)
                        resolved = posixpath.normpath(posixpath.join(posixpath.dirname(name), target))
                        if resolved.startswith("../") or resolved == "..":
                            raise ValueError(f"{path.name}: unsafe symlink {name!r}")
                        if prefix is not None and resolved != prefix and not resolved.startswith(prefix + "/"):
                            raise ValueError(f"{path.name}: unsafe symlink {name!r}")
                    links.add(name)
                elif member.islnk():
                    target = archive_name(member.linkname, path.name)
                    if prefix is not None and target != prefix and not target.startswith(prefix + "/"):
                        raise ValueError(f"{path.name}: unsafe hardlink {name!r}")
                    links.add(name)
            for name in members:
                if any(name.startswith(link + "/") for link in links):
                    raise ValueError(f"{path.name}: archive path below link {name!r}")
    except (OSError, tarfile.TarError) as exc:
        raise ValueError(f"{path.name}: invalid tar archive: {exc}") from exc
    return members


def inspect_zip(path, prefix):
    members = {}
    try:
        with zipfile.ZipFile(path) as archive:
            for member in archive.infolist():
                name = archive_name(member.filename, path.name)
                if name != prefix and not name.startswith(prefix + "/"):
                    raise ValueError(f"{path.name}: unsafe archive path {member.filename!r}")
                if name in members:
                    raise ValueError(f"{path.name}: duplicate archive path {name!r}")
                mode = member.external_attr >> 16
                if stat.S_ISLNK(mode):
                    raise ValueError(f"{path.name}: symlink in ZIP {name!r}")
                members[name] = member
            bad = archive.testzip()
            if bad is not None:
                raise ValueError(f"{path.name}: failed ZIP CRC for {bad}")
    except (OSError, zipfile.BadZipFile) as exc:
        raise ValueError(f"{path.name}: invalid ZIP archive: {exc}") from exc
    return members


def digest_stream(stream, first=b""):
    hasher = hashlib.sha256(first)
    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
        hasher.update(chunk)
    return hasher.hexdigest()


def check_container(path, expected_type):
    try:
        result = subprocess.run(
            ["7z", "t", "-bd", str(path)], capture_output=True,
            text=True, timeout=180, check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ValueError(f"{path.name}: cannot test {expected_type} payload: {error}") from error
    if (result.returncode != 0 or f"Type = {expected_type}" not in result.stdout
            or "Everything is Ok" not in result.stdout):
        raise ValueError(f"{path.name}: invalid {expected_type} payload")


def read_container_member(path, expected_name):
    """Read exactly one regular installer entry selected by its full path."""
    try:
        listing = subprocess.run(
            ["7z", "l", "-slt", "-bd", str(path)], capture_output=True,
            text=True, timeout=180, check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ValueError(f"{path.name}: cannot list installer members: {error}") from error
    separator = "\n----------\n"
    if listing.returncode or separator not in listing.stdout:
        raise ValueError(f"{path.name}: cannot list installer members")
    requested = expected_name.casefold()
    matches = []
    for block in listing.stdout.rsplit(separator, 1)[1].strip().split("\n\n"):
        fields = dict(line.split(" = ", 1) for line in block.splitlines()
                      if " = " in line)
        name = fields.get("Path", "")
        normalized = posixpath.normpath(name.replace("\\", "/")).casefold()
        if normalized == requested:
            matches.append(fields)
    if len(matches) != 1 or matches[0].get("Path") != expected_name:
        raise ValueError(f"{path.name}: missing or ambiguous installer member {expected_name}")
    member = matches[0]
    try:
        size = int(member["Size"])
    except (KeyError, ValueError) as error:
        raise ValueError(f"{path.name}: invalid installer member size {expected_name}") from error
    if (size < MIN_BINARY_SIZE or size > MAX_CONTAINER_MEMBER_SIZE
            or member.get("Folder") == "+" or member.get("Symbolic Link", "")):
        raise ValueError(f"{path.name}: invalid installer member {expected_name}")
    try:
        result = subprocess.run(
            ["7z", "e", "-so", "-bd", str(path), expected_name],
            capture_output=True, timeout=180, check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ValueError(f"{path.name}: cannot extract installer member {expected_name}: {error}") from error
    if result.returncode or len(result.stdout) != size:
        raise ValueError(f"{path.name}: invalid extracted installer member {expected_name}")
    return result.stdout


def list_dmg_app_entries(path):
    """Inventory the installed app, rejecting path aliases and duplicates."""
    try:
        listing = subprocess.run(
            ["7z", "l", "-slt", "-bd", str(path)], capture_output=True,
            text=True, timeout=180, check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ValueError(f"{path.name}: cannot list DMG app: {error}") from error
    separator = "\n----------\n"
    if listing.returncode or separator not in listing.stdout:
        raise ValueError(f"{path.name}: cannot list DMG app")
    root = "Raven-Qt.app"
    entries = {}
    folded = set()
    for block in listing.stdout.rsplit(separator, 1)[1].strip().split("\n\n"):
        fields = dict(line.split(" = ", 1) for line in block.splitlines()
                      if " = " in line)
        name = fields.get("Path", "")
        normalized = posixpath.normpath(name.replace("\\", "/"))
        if (normalized.casefold() == root.casefold()
                or normalized.casefold().startswith(root.casefold() + "/")):
            if (not (name == root or name.startswith(root + "/"))
                    or archive_name(name, path.name) != name
                    or name.casefold() in folded):
                raise ValueError(f"{path.name}: unsafe or ambiguous DMG app path {name!r}")
            folded.add(name.casefold())
            entries[name] = fields
    if root not in entries:
        raise ValueError(f"{path.name}: missing DMG app")
    return entries


def extract_dmg_app_file(path, name, size):
    if size < 0 or size > MAX_CONTAINER_MEMBER_SIZE:
        raise ValueError(f"{path.name}: invalid DMG app file size {name}")
    try:
        result = subprocess.run(
            ["7z", "e", "-so", "-bd", str(path), name],
            capture_output=True, timeout=180, check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ValueError(f"{path.name}: cannot extract DMG app file {name}: {error}") from error
    if result.returncode or len(result.stdout) != size:
        raise ValueError(f"{path.name}: invalid extracted DMG app file {name}")
    return result.stdout


def verify_dmg_app(dmg, signing, signing_members):
    """Bind every installed app entry to the unsigned signing archive."""
    prefix = "dist/Raven-Qt.app"
    expected = {name[5:]: member for name, member in signing_members.items()
                if name == prefix or name.startswith(prefix + "/")}
    actual = list_dmg_app_entries(dmg)
    if set(actual) != set(expected):
        raise ValueError("osx: DMG app inventory differs from signing archive")
    with tarfile.open(signing, "r:gz") as archive:
        for name, member in expected.items():
            fields = actual[name]
            if member.isdir():
                if fields.get("Folder") != "+" or fields.get("Symbolic Link", ""):
                    raise ValueError(f"osx: DMG app type differs: {name}")
            elif member.issym():
                if (fields.get("Folder") == "+"
                        or fields.get("Symbolic Link") != member.linkname):
                    raise ValueError(f"osx: DMG app link differs: {name}")
            elif member.isfile():
                try:
                    size = int(fields["Size"])
                except (KeyError, ValueError) as error:
                    raise ValueError(f"osx: invalid DMG app file size: {name}") from error
                if (fields.get("Folder") != "-" or fields.get("Symbolic Link", "")
                        or size != member.size):
                    raise ValueError(f"osx: DMG app type or size differs: {name}")
                installed = extract_dmg_app_file(dmg, name, size)
                with archive.extractfile(member) as stream:
                    if hashlib.sha256(installed).digest() != hashlib.sha256(stream.read()).digest():
                        raise ValueError(f"osx: DMG app differs from signing archive: {name}")
            else:
                raise ValueError(f"osx: unsupported signing app entry: {name}")
    return len(actual)


def pe_sections(payload, name):
    offset = struct.unpack_from("<I", payload, 0x3C)[0]
    count = struct.unpack_from("<H", payload, offset + 6)[0]
    optional_size = struct.unpack_from("<H", payload, offset + 20)[0]
    table = offset + 24 + optional_size
    if table + count * 40 > len(payload):
        raise ValueError(f"windows: invalid PE section table in {name}")
    sections = []
    for index in range(count):
        start = table + index * 40
        record = payload[start:start + 40]
        raw_size, raw_offset = struct.unpack_from("<II", record, 16)
        if raw_size and (raw_offset == 0 or raw_offset + raw_size > len(payload)):
            raise ValueError(f"windows: invalid PE section extent in {name}")
        sections.append((record, payload[raw_offset:raw_offset + raw_size]))
    return offset, table, sections


def pe_section_name(payload, offset, record):
    name = record[:8].split(b"\0", 1)[0]
    if not name.startswith(b"/"):
        return name
    try:
        index = int(name[1:])
    except ValueError as error:
        raise ValueError("windows: invalid PE long section name") from error
    symbols = struct.unpack_from("<I", payload, offset + 12)[0]
    count = struct.unpack_from("<I", payload, offset + 16)[0]
    string_table = symbols + count * 18
    if symbols == 0 or string_table + 4 > len(payload):
        raise ValueError("windows: missing PE section string table")
    length = struct.unpack_from("<I", payload, string_table)[0]
    if index < 4 or index >= length or string_table + length > len(payload):
        raise ValueError("windows: invalid PE section string table")
    start = string_table + index
    end = payload.find(b"\0", start, string_table + length)
    if end < 0:
        raise ValueError("windows: unterminated PE section name")
    return payload[start:end]


def pe_coff_strings(payload, offset, expected_start):
    symbols, count = struct.unpack_from("<II", payload, offset + 12)
    if count:
        raise ValueError("windows: unexpected PE symbol table")
    if symbols == 0:
        if len(payload) != expected_start:
            raise ValueError("windows: unexplained PE overlay")
        return b""
    if symbols != expected_start or symbols + 4 > len(payload):
        raise ValueError("windows: unexpected PE string table position")
    length = struct.unpack_from("<I", payload, symbols)[0]
    if length < 4 or symbols + length != len(payload):
        raise ValueError("windows: invalid PE string table extent")
    return payload[symbols + 4:]


def compare_installed_pe(primary, installed, name):
    """Allow only removal of the primary ZIP's final GNU debuglink section."""
    check_binary(installed[:HEADER_READ_SIZE], "pe", 0x8664, "windows", name,
                 len(installed))
    for payload in (primary, installed):
        offset = struct.unpack_from("<I", payload, 0x3C)[0]
        optional = offset + 24
        if (struct.unpack_from("<H", payload, offset + 20)[0] < 152
                or struct.unpack_from("<I", payload, optional + 108)[0] < 5
                or struct.unpack_from("<II", payload, optional + 144) != (0, 0)):
            raise ValueError(f"windows: unexpected PE certificate table: {name}")
    if primary == installed:
        return
    source_offset, source_table, source_sections = pe_sections(primary, name)
    target_offset, target_table, target_sections = pe_sections(installed, name)
    if (source_offset != target_offset or source_table != target_table
            or len(source_sections) != len(target_sections) + 1
            or pe_section_name(primary, source_offset, source_sections[-1][0]) != b".gnu_debuglink"):
        raise ValueError(f"windows: installer executable differs from primary: {name}")
    debug_record, _ = source_sections[-1]
    debug_flags = struct.unpack_from("<I", debug_record, 36)[0]
    if debug_flags & 0x20000000 or any(a != b for a, b in zip(source_sections, target_sections)):
        raise ValueError(f"windows: installer executable differs from primary: {name}")
    common_end = max(struct.unpack_from("<II", record, 16)[0]
                     + struct.unpack_from("<II", record, 16)[1]
                     for record, _ in target_sections)
    debug_size, debug_offset = struct.unpack_from("<II", debug_record, 16)
    extra_start = source_table + len(target_sections) * 40
    if (debug_size == 0 or debug_offset != common_end
            or any(installed[extra_start:extra_start + 40])
            or primary[extra_start + 40:common_end] != installed[extra_start + 40:common_end]):
        raise ValueError(f"windows: installer executable differs from primary: {name}")
    installed_strings = pe_coff_strings(installed, target_offset, common_end)
    primary_strings = pe_coff_strings(primary, source_offset,
                                      debug_offset + debug_size)
    if primary_strings != installed_strings + b".gnu_debuglink\0":
        raise ValueError(f"windows: installer executable differs from primary: {name}")
    source_header = bytearray(primary[:source_table])
    target_header = bytearray(installed[:target_table])
    for header, offset in ((source_header, source_offset),
                           (target_header, target_offset)):
        header[offset + 6:offset + 8] = b"\0\0"  # section count
        header[offset + 12:offset + 20] = b"\0" * 8  # COFF string table
        flags = struct.unpack_from("<H", header, offset + 22)[0]
        struct.pack_into("<H", header, offset + 22, flags & ~0x0200)
        header[offset + 80:offset + 84] = b"\0" * 4  # SizeOfImage
        header[offset + 88:offset + 92] = b"\0" * 4  # CheckSum
    if source_header != target_header:
        raise ValueError(f"windows: installer executable differs from primary: {name}")


def check_binary(header, kind, machine, target, name, size, installer=False):
    minimum = MIN_INSTALLER_SIZE if installer else MIN_BINARY_SIZE
    if size < minimum:
        raise ValueError(f"{target}: small executable {name}: {size} bytes")
    if kind == "elf":
        expected_class = 1 if target == "arm32v7" else 2
        if len(header) < 20 or header[:4] != b"\x7fELF" or header[4:6] != bytes((expected_class, 1)):
            raise ValueError(f"{target}: invalid ELF header for {name}")
        actual = struct.unpack_from("<H", header, 18)[0]
        if (header[6] != 1 or struct.unpack_from("<H", header, 16)[0] not in (2, 3)
                or struct.unpack_from("<I", header, 20)[0] != 1):
            raise ValueError(f"{target}: invalid ELF executable header for {name}")
        is_64 = expected_class == 2
        entry = struct.unpack_from("<Q" if is_64 else "<I", header, 24)[0]
        phoff = struct.unpack_from("<Q" if is_64 else "<I", header,
                                   32 if is_64 else 28)[0]
        phentsize, phnum = struct.unpack_from("<HH", header, 54 if is_64 else 42)
        expected_phentsize = 56 if is_64 else 32
        if (entry == 0 or phoff == 0 or phnum == 0 or phnum > 256
                or phentsize < expected_phentsize
                or phoff + phentsize * phnum > min(size, len(header))):
            raise ValueError(f"{target}: invalid ELF program header table for {name}")
        executable_load = False
        for index in range(phnum):
            offset = phoff + index * phentsize
            program_type = struct.unpack_from("<I", header, offset)[0]
            if is_64:
                flags = struct.unpack_from("<I", header, offset + 4)[0]
                file_offset, file_size = struct.unpack_from("<QQ", header, offset + 8)[0], struct.unpack_from("<Q", header, offset + 32)[0]
            else:
                file_offset, file_size = struct.unpack_from("<II", header, offset + 4)[0], struct.unpack_from("<I", header, offset + 16)[0]
                flags = struct.unpack_from("<I", header, offset + 24)[0]
            if program_type == 1 and flags & 1 and file_size > 0 and file_offset + file_size <= size:
                executable_load = True
        if not executable_load:
            raise ValueError(f"{target}: no executable ELF program header for {name}")
    elif kind == "macho":
        if len(header) < 32 or header[:4] != b"\xcf\xfa\xed\xfe":
            raise ValueError(f"{target}: invalid Mach-O header for {name}")
        actual = struct.unpack_from("<I", header, 4)[0]
        filetype, command_count, command_bytes = struct.unpack_from("<III", header, 12)
        if (filetype != 2 or command_count == 0 or command_count > 256
                or command_bytes == 0 or 32 + command_bytes > min(size, len(header))):
            raise ValueError(f"{target}: invalid Mach-O load command table for {name}")
        offset = 32
        text_segment = False
        entry_command = False
        end = 32 + command_bytes
        for _ in range(command_count):
            if offset + 8 > end:
                raise ValueError(f"{target}: invalid Mach-O load command for {name}")
            command, command_size = struct.unpack_from("<II", header, offset)
            if command_size < 8 or offset + command_size > end:
                raise ValueError(f"{target}: invalid Mach-O load command for {name}")
            if command == 0x19 and command_size >= 72:
                segment = header[offset + 8:offset + 24].split(b"\0", 1)[0]
                file_offset, file_size = struct.unpack_from("<QQ", header, offset + 40)
                initial_protection = struct.unpack_from("<I", header, offset + 60)[0]
                if (segment == b"__TEXT" and file_size > 0
                        and file_offset + file_size <= size and initial_protection & 4):
                    text_segment = True
            if command == 0x80000028 and command_size >= 24:
                entry_offset = struct.unpack_from("<Q", header, offset + 8)[0]
                entry_command = 0 < entry_offset < size
            elif command == 0x5:
                entry_command = True
            offset += command_size
        if offset != end or not text_segment or not entry_command:
            raise ValueError(f"{target}: invalid Mach-O load commands for {name}")
    else:
        if len(header) < 64 or header[:2] != b"MZ":
            raise ValueError(f"{target}: invalid PE header for {name}")
        offset = struct.unpack_from("<I", header, 0x3C)[0]
        if offset + 24 > len(header) or header[offset:offset + 4] != b"PE\0\0":
            raise ValueError(f"{target}: invalid PE signature for {name}")
        actual = struct.unpack_from("<H", header, offset + 4)[0]
        section_count = struct.unpack_from("<H", header, offset + 6)[0]
        optional_size = struct.unpack_from("<H", header, offset + 20)[0]
        optional_offset = offset + 24
        expected_magic = 0x10B if installer else 0x20B
        if (section_count == 0 or section_count > 96 or optional_size < 96
                or optional_offset + optional_size + section_count * 40 > len(header)
                or struct.unpack_from("<H", header, optional_offset)[0] != expected_magic
                or struct.unpack_from("<I", header, optional_offset + 16)[0] == 0):
            raise ValueError(f"{target}: invalid PE section table for {name}")
        executable_section = False
        section_offset = optional_offset + optional_size
        for index in range(section_count):
            section = section_offset + index * 40
            raw_size, raw_offset = struct.unpack_from("<II", header, section + 16)
            flags = struct.unpack_from("<I", header, section + 36)[0]
            if flags & 0x20000000 and raw_size > 0 and raw_offset + raw_size <= size:
                executable_section = True
        if not executable_section:
            raise ValueError(f"{target}: no executable section in {name}")
        if installer and b"Nullsoft" not in header:
            raise ValueError(f"{target}: installer lacks the NSIS signature in {name}")
    if actual != machine:
        raise ValueError(f"{target}: wrong binary machine for {name}: {actual:#x}, expected {machine:#x}")


def check_primary(path, target, dist):
    _, _, machine, kind = TARGETS[target]
    binary_names = ("ravend", "raven-cli", "raven-qt")
    binary_hashes = {}
    if kind == "pe":
        members = inspect_zip(path, dist)
        with zipfile.ZipFile(path) as archive:
            for binary in binary_names:
                name = f"{dist}/bin/{binary}.exe"
                if name not in members:
                    raise ValueError(f"{target}: missing binary {name}")
                with archive.open(members[name]) as stream:
                    header = stream.read(HEADER_READ_SIZE)
                    check_binary(header, kind, machine, target, name,
                                 members[name].file_size)
                    binary_hashes[binary] = digest_stream(stream, header)
    else:
        members = inspect_tar(path, dist)
        with tarfile.open(path, "r:gz") as archive:
            for binary in binary_names:
                name = f"{dist}/bin/{binary}"
                if name not in members or not members[name].isfile():
                    raise ValueError(f"{target}: missing binary {name}")
                with archive.extractfile(members[name]) as stream:
                    header = stream.read(HEADER_READ_SIZE)
                    check_binary(header, kind, machine, target, name,
                                 members[name].size)
                    binary_hashes[binary] = digest_stream(stream, header)
    return {"format": kind, "machine": f"{machine:#x}",
            "archive_paths_safe": True, "binary_sha256": binary_hashes}


def source_commit(path):
    with tempfile.TemporaryFile() as tar:
        with gzip.open(path, "rb") as source:
            shutil.copyfileobj(source, tar)
        tar.seek(0)
        result = subprocess.run(["git", "get-tar-commit-id"], stdin=tar,
                                capture_output=True, text=True)
        if result.returncode:
            raise ValueError(f"{path.name}: cannot read embedded source commit: {result.stderr.strip()}")
    return result.stdout.strip()


def check_source_matches_checkout(path, repository, sha, dist):
    head = subprocess.run(["git", "-C", str(repository), "rev-parse", "HEAD"],
                          capture_output=True, text=True, check=True).stdout.strip()
    if head != sha:
        raise ValueError(f"checked-out commit {head} differs from {sha}")
    with gzip.open(path, "rb") as stream:
        source_hash = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            source_hash.update(chunk)
    command = ["git", "-C", str(repository), "archive", "--format=tar",
               f"--prefix={dist}/", sha]
    archive_hash = hashlib.sha256()
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
        for chunk in iter(lambda: process.stdout.read(1024 * 1024), b""):
            archive_hash.update(chunk)
        error = process.stderr.read()
        if process.wait():
            raise ValueError(f"cannot archive checked-out commit: {error.decode(errors='replace').strip()}")
    if source_hash.digest() != archive_hash.digest():
        raise ValueError(f"{path.name}: source archive content differs from checked-out commit {sha}")


def parse_provenance(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    if len(lines) != 5 or any("=" not in line for line in lines):
        raise ValueError(f"{path}: invalid provenance")
    fields = dict(line.split("=", 1) for line in lines)
    if len(fields) != 5:
        raise ValueError(f"{path}: duplicate provenance field")
    return fields


def parse_manifest(path):
    records = {}
    for line in path.read_text(encoding="ascii").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9._-]*)", line)
        if match is None or match[2] in records:
            raise ValueError(f"{path}: invalid manifest entry {line!r}")
        records[match[2]] = match[1]
    return records


def check_sidecars(bundle, artifact):
    for algorithm in ("sha256", "md5"):
        name = f"{artifact}.{algorithm}sum"
        path = bundle / name
        if path.exists():
            length = 64 if algorithm == "sha256" else 32
            expected = f"{digest(bundle / artifact, algorithm)}  {artifact}\n"
            if path.read_text(encoding="ascii") != expected or len(expected.split("  ")[0]) != length:
                raise ValueError(f"{bundle.name}: incorrect {algorithm} sidecar for {artifact}")


def distribution_from_checkout(repository, sha, packaging_ref):
    head = subprocess.run(["git", "-C", str(repository), "rev-parse", "HEAD"],
                          capture_output=True, text=True, check=True).stdout.strip()
    if head != sha:
        raise ValueError(f"checked-out commit {head} differs from {sha}")
    source = (repository / "configure.ac").read_text(encoding="utf-8")
    version = []
    for field in ("MAJOR", "MINOR", "REVISION", "BUILD", "RC"):
        match = re.search(r"(?m)^define\(_CLIENT_VERSION_" + field + r", ([0-9]+)\)$", source)
        if match is None:
            raise ValueError(f"cannot read CLIENT_VERSION_{field} from configure.ac")
        version.append(int(match[1]))
    major, minor, revision, build, rc = version
    package_version = f"{major}.{minor}.{revision}"
    if build:
        package_version += f".{build}"
    if rc:
        package_version += f"test{rc}"
    dist = "raven-" + package_version
    if "release" not in packaging_ref:
        short = subprocess.run(["git", "-C", str(repository), "rev-parse", "--short", "HEAD"],
                               capture_output=True, text=True, check=True).stdout.strip()
        dist += "-" + short
    return dist


def verify(root, sha, dist, repository=None):
    if re.fullmatch(r"[0-9a-f]{40}", sha) is None:
        raise ValueError("source SHA must be a full lowercase commit ID")
    if re.fullmatch(r"raven-[0-9A-Za-z.-]+", dist) is None or ".." in dist:
        raise ValueError("invalid distribution name")
    expected_dirs = {f"raven-unsigned-{target}-{sha}" for target in TARGETS}
    found_dirs = {entry.name for entry in root.iterdir()}
    if found_dirs != expected_dirs:
        raise ValueError(f"bundle directories differ: missing={expected_dirs - found_dirs}, extra={found_dirs - expected_dirs}")

    source_name = dist + ".tar.gz"
    source_hashes = set()
    records = []
    for target, (host, suffixes, _, _) in TARGETS.items():
        bundle = root / f"raven-unsigned-{target}-{sha}"
        if not bundle.is_dir() or bundle.is_symlink():
            raise ValueError(f"{target}: bundle is not a regular directory")
        expected_provenance = {
            "commit": sha, "target": target, "host": host,
            "source_archive": source_name, "status": STATUS,
        }
        provenance = parse_provenance(bundle / "PROVENANCE.txt")
        if provenance != expected_provenance:
            raise ValueError(f"{target}: unexpected provenance {provenance}")
        artifact_names = {dist + suffix for suffix in suffixes}
        expected_names = artifact_names | {source_name, "PROVENANCE.txt"}
        hashes = parse_manifest(bundle / "SHA256SUMS")
        if set(hashes) != expected_names:
            raise ValueError(f"{target}: manifest files differ: {set(hashes) ^ expected_names}")
        sidecars = {f"{name}.{algorithm}sum" for name in artifact_names
                    for algorithm in ("sha256", "md5")}
        allowed_names = expected_names | sidecars | {"SHA256SUMS"}
        if target == "windows":
            allowed_names.add("windeploy")
        found_names = {entry.name for entry in bundle.iterdir()}
        if found_names - allowed_names:
            raise ValueError(f"{target}: unexpected bundle files {found_names - allowed_names}")
        for entry in bundle.iterdir():
            if target == "windows" and entry.name == "windeploy":
                continue
            if not entry.is_file() or entry.is_symlink() or entry.stat().st_size == 0:
                raise ValueError(f"{target}: invalid bundle file {entry.name}")
        for name, reported in hashes.items():
            if digest(bundle / name) != reported:
                raise ValueError(f"{target}: checksum mismatch for {name}")
        for name in artifact_names:
            check_sidecars(bundle, name)

        primary = bundle / (dist + suffixes[0])
        binary = check_primary(primary, target, dist)
        if target == "windows":
            # The unsigned NSIS installer uses a 32-bit PE launcher around 64-bit payloads.
            installer = bundle / (dist + suffixes[1])
            with installer.open("rb") as stream:
                check_binary(stream.read(HEADER_READ_SIZE), "pe", 0x14C,
                             target, installer.name, installer.stat().st_size,
                             installer=True)
            check_container(installer, "Nsis")
            installed_hashes = {}
            with zipfile.ZipFile(primary) as archive:
                for binary_name, installed_name in (
                        ("ravend", "daemon/ravend.exe"),
                        ("raven-cli", "daemon/raven-cli.exe"),
                        ("raven-qt", "raven-qt.exe")):
                    zip_name = f"{dist}/bin/{binary_name}.exe"
                    source_binary = archive.read(zip_name)
                    installed_binary = read_container_member(installer, installed_name)
                    compare_installed_pe(source_binary, installed_binary, installed_name)
                    installed_hashes[binary_name] = hashlib.sha256(installed_binary).hexdigest()
            binary["installer_binary_sha256"] = installed_hashes
            signing = bundle / (dist + suffixes[2])
            signing_members = inspect_tar(signing)
            deploy = bundle / "windeploy"
            if not deploy.is_dir() or deploy.is_symlink():
                raise ValueError("windows: missing or invalid windeploy directory")
            expected_deploy = {"win-codesign.cert", "detached-sig-create.sh"}
            if {entry.name for entry in deploy.iterdir()} != expected_deploy:
                raise ValueError("windows: unexpected windeploy contents")
            with tarfile.open(signing, "r:gz") as archive:
                for name in expected_deploy:
                    file = deploy / name
                    member = signing_members.get(name)
                    if member is None or not member.isfile() or member.size == 0:
                        raise ValueError(f"windows: missing signing bundle entry: {name}")
                    if (not file.is_file() or file.is_symlink()
                            or archive.extractfile(member).read() != file.read_bytes()):
                        raise ValueError(f"windows: windeploy differs from signing bundle: {name}")
        elif target == "osx":
            dmg = bundle / (dist + suffixes[1])
            if dmg.stat().st_size < MIN_INSTALLER_SIZE:
                raise ValueError(f"{target}: small DMG payload")
            with dmg.open("rb") as stream:
                stream.seek(-512, os.SEEK_END)
                if stream.read(4) != b"koly":
                    raise ValueError(f"{target}: invalid DMG trailer")
            check_container(dmg, "Dmg")
            installed_hashes = {}
            for binary_name, app_name in (("ravend", "ravend"),
                                          ("raven-cli", "raven-cli"),
                                          ("raven-qt", "Raven-Qt")):
                installed_name = "Raven-Qt.app/Contents/MacOS/" + app_name
                installed_binary = read_container_member(dmg, installed_name)
                check_binary(installed_binary[:HEADER_READ_SIZE], "macho", 0x01000007,
                             target, installed_name, len(installed_binary))
                installed_hash = hashlib.sha256(installed_binary).hexdigest()
                if installed_hash != binary["binary_sha256"][binary_name]:
                    raise ValueError(f"osx: DMG app differs from primary: {installed_name}")
                installed_hashes[binary_name] = installed_hash
            binary["installer_binary_sha256"] = installed_hashes
            signing = bundle / (dist + suffixes[2])
            signing_members = inspect_tar(signing, mac_signing_bundle=True)
            app_prefix = "dist/Raven-Qt.app/Contents"
            info = signing_members.get(app_prefix + "/Info.plist")
            if info is None or not info.isfile() or info.size == 0:
                raise ValueError("osx: missing signing app metadata")
            with tarfile.open(signing, "r:gz") as archive:
                for primary_name, app_name in (("ravend", "ravend"),
                                               ("raven-cli", "raven-cli"),
                                               ("raven-qt", "Raven-Qt")):
                    name = app_prefix + "/MacOS/" + app_name
                    member = signing_members.get(name)
                    if member is None or not member.isfile() or member.size < MIN_BINARY_SIZE:
                        raise ValueError(f"osx: missing signing app binary: {name}")
                    with archive.extractfile(member) as stream:
                        if digest_stream(stream) != binary["binary_sha256"][primary_name]:
                            raise ValueError(f"osx: signing app differs from primary: {name}")
            binary["dmg_app_entries_verified"] = verify_dmg_app(dmg, signing, signing_members)

        source_hashes.add(hashes[source_name])
        records.append({
            "target": target, "host": host, "bundle": bundle.name,
            "manifest_verified": True, "provenance_verified": True,
            "source_sha256": hashes[source_name], "binary": binary,
            "artifact_sha256": {name: hashes[name] for name in sorted(artifact_names)},
        })
    if len(source_hashes) != 1:
        raise ValueError(f"source archives differ: {sorted(source_hashes)}")
    source = root / f"raven-unsigned-linux-{sha}" / source_name
    inspect_tar(source, dist)
    commit = source_commit(source)
    if commit != sha:
        raise ValueError(f"source archive commit {commit} differs from {sha}")
    if repository is not None:
        check_source_matches_checkout(source, repository, sha, dist)
    return {"source_sha": sha, "distribution": dist,
            "source_sha256": next(iter(source_hashes)),
            "checkout_source_verified": repository is not None, "targets": records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("source_sha")
    parser.add_argument("distribution", nargs="?")
    parser.add_argument("--repository", type=Path)
    parser.add_argument("--package-ref")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.distribution is None:
        if args.repository is None or args.package_ref is None:
            parser.error("provide a distribution name or both --repository and --package-ref")
        dist = distribution_from_checkout(args.repository, args.source_sha, args.package_ref)
    else:
        dist = args.distribution
    result = verify(args.directory, args.source_sha, dist, args.repository)
    payload = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(payload, encoding="utf-8")
    print(payload, end="")


if __name__ == "__main__":
    main()
