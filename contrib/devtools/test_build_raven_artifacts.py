#!/usr/bin/env python3
"""Negative controls for the downloaded Build Raven release bundles."""

import gzip
import hashlib
import io
import os
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock
import zipfile

import verify_build_raven_artifacts as verifier


SHA = "a" * 40
DIST = "raven-4.8.1-aaaaaaaaa"
STATUS = "unsigned CI build; Windows and macOS outputs are not signed final releases"
TEST_BINARY_SIZE = 128 * 1024
TEST_INSTALLER_SIZE = 1024 * 1024


def elf(machine, elf_class):
    header = bytearray(TEST_BINARY_SIZE)
    header[:7] = b"\x7fELF" + bytes((elf_class, 1, 1))
    struct.pack_into("<H", header, 16, 3)
    struct.pack_into("<H", header, 18, machine)
    struct.pack_into("<I", header, 20, 1)
    if elf_class == 1:
        struct.pack_into("<II", header, 24, 0x1000, 52)
        struct.pack_into("<HH", header, 42, 32, 1)
        struct.pack_into("<IIIIIIII", header, 52, 1, 0, 0x1000, 0x1000,
                         TEST_BINARY_SIZE, TEST_BINARY_SIZE, 5, 0x1000)
    else:
        struct.pack_into("<QQ", header, 24, 0x1000, 64)
        struct.pack_into("<HH", header, 54, 56, 1)
        struct.pack_into("<IIQQQQQQ", header, 64, 1, 5, 0, 0x1000, 0x1000,
                         TEST_BINARY_SIZE, TEST_BINARY_SIZE, 0x1000)
    return bytes(header)


def macho():
    header = bytearray(TEST_BINARY_SIZE)
    struct.pack_into("<IIIIIIII", header, 0, 0xFEEDFACF, 0x01000007,
                     3, 2, 2, 96, 0, 0)
    struct.pack_into("<II16sQQQQIIII", header, 32, 0x19, 72,
                     b"__TEXT", 0x100000000, TEST_BINARY_SIZE, 0,
                     TEST_BINARY_SIZE, 7, 5, 0, 0)
    struct.pack_into("<IIQQ", header, 104, 0x80000028, 24, 4096, 0)
    return bytes(header)


def pe(machine, size=TEST_BINARY_SIZE):
    header = bytearray(size)
    header[:2] = b"MZ"
    struct.pack_into("<I", header, 0x3C, 128)
    header[128:132] = b"PE\0\0"
    struct.pack_into("<H", header, 132, machine)
    struct.pack_into("<H", header, 134, 1)
    optional_size = 224 if machine == 0x14C else 240
    struct.pack_into("<H", header, 148, optional_size)
    struct.pack_into("<H", header, 152, 0x10B if machine == 0x14C else 0x20B)
    struct.pack_into("<I", header, 168, 0x1000)
    if machine != 0x14C:
        struct.pack_into("<I", header, 152 + 108, 16)
    section = 152 + optional_size
    struct.pack_into("<8sIIIIIIHHI", header, section, b".text", 0x1000,
                     0x1000, 0x10000, 0x200, 0, 0, 0, 0, 0x60000020)
    if machine == 0x14C:
        header[116900:116908] = b"Nullsoft"
    return bytes(header)


def pe_with_debuglink(installed):
    primary = bytearray(installed + bytes(512) + b"\x13\0\0\0.gnu_debuglink\0")
    offset = struct.unpack_from("<I", primary, 0x3C)[0]
    struct.pack_into("<H", primary, offset + 6, 2)
    struct.pack_into("<I", primary, offset + 12, len(installed) + 512)
    struct.pack_into("<H", primary, offset + 22, 0x2E)
    struct.pack_into("<I", primary, offset + 80, 0x3000)
    struct.pack_into("<I", primary, offset + 88, 1)
    section = offset + 24 + 240 + 40
    struct.pack_into("<8sIIIIIIHHI", primary, section, b"/4", 16, 0x2000,
                     512, len(installed), 0, 0, 0, 0, 0x42000040)
    primary[len(installed):len(installed) + 16] = b"ravend.exe.dbg\0\0"
    return bytes(primary)


def archive(members, commit=None):
    buffer = io.BytesIO()
    pax = {"comment": commit} if commit is not None else {}
    with tarfile.open(fileobj=buffer, mode="w", format=tarfile.PAX_FORMAT,
                      pax_headers=pax) as tar:
        for name, content in members:
            entry = tarfile.TarInfo(name)
            if name.endswith("/"):
                entry.type = tarfile.DIRTYPE
                tar.addfile(entry)
            else:
                entry.size = len(content)
                tar.addfile(entry, io.BytesIO(content))
    return gzip.compress(buffer.getvalue(), mtime=0)


def payloads(source):
    output = {}
    for target, (_, suffixes, machine, kind) in verifier.TARGETS.items():
        files = {DIST + ".tar.gz": source}
        primary = DIST + suffixes[0]
        if kind == "pe":
            buffer = io.BytesIO()
            with zipfile.ZipFile(buffer, "w") as z:
                for binary in ("ravend.exe", "raven-cli.exe", "raven-qt.exe"):
                    z.writestr(f"{DIST}/bin/{binary}", pe(machine))
            files[primary] = buffer.getvalue()
            files[DIST + suffixes[1]] = pe(0x14C, TEST_INSTALLER_SIZE)
            files[DIST + suffixes[2]] = archive([
                ("./detached-sig-create.sh", b"test"),
                ("./win-codesign.cert", b"cert"),
            ])
        else:
            binary = macho() if kind == "macho" else elf(
                machine, 1 if target == "arm32v7" else 2
            )
            files[primary] = archive(
                [(f"{DIST}/bin/{name}", binary) for name in
                 ("ravend", "raven-cli", "raven-qt")]
            )
            if target == "osx":
                files[DIST + suffixes[1]] = bytes(TEST_INSTALLER_SIZE) + b"koly" + bytes(508)
                files[DIST + suffixes[2]] = archive([
                    ("./dist/Raven-Qt.app/", b""),
                    ("./dist/Raven-Qt.app/Contents/", b""),
                    ("./dist/Raven-Qt.app/Contents/MacOS/", b""),
                    ("./dist/Raven-Qt.app/Contents/Resources/", b""),
                    ("./dist/Raven-Qt.app/Contents/Info.plist", b"test"),
                    ("./dist/Raven-Qt.app/Contents/MacOS/Raven-Qt", binary),
                    ("./dist/Raven-Qt.app/Contents/MacOS/raven-cli", binary),
                    ("./dist/Raven-Qt.app/Contents/MacOS/ravend", binary),
                    ("./dist/Raven-Qt.app/Contents/Resources/qt.conf", b"Plugins=PlugIns\n"),
                ])
        output[target] = files
    return output


class ArtifactVerificationTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory(prefix="rip25-artifact-test-")
        self.root = Path(self.tempdir.name)
        source = archive([(f"{DIST}/README", b"source")], commit=SHA)
        self.files = payloads(source)
        self.installed = {
            "daemon/ravend.exe": pe(0x8664),
            "daemon/raven-cli.exe": pe(0x8664),
            "raven-qt.exe": pe(0x8664),
            "Raven-Qt.app/Contents/MacOS/ravend": macho(),
            "Raven-Qt.app/Contents/MacOS/raven-cli": macho(),
            "Raven-Qt.app/Contents/MacOS/Raven-Qt": macho(),
            "Raven-Qt.app/Contents/Info.plist": b"test",
            "Raven-Qt.app/Contents/Resources/qt.conf": b"Plugins=PlugIns\n",
        }
        self.dmg_entries = {
            name: {"Path": name, "Folder": "+", "Symbolic Link": ""}
            for name in ("Raven-Qt.app", "Raven-Qt.app/Contents",
                         "Raven-Qt.app/Contents/MacOS",
                         "Raven-Qt.app/Contents/Resources")
        }
        self.dmg_entries.update({
            name: {"Path": name, "Folder": "-", "Size": str(len(content)),
                   "Symbolic Link": ""}
            for name, content in self.installed.items()
            if name.startswith("Raven-Qt.app/")
        })
        self.write_bundles()

    def tearDown(self):
        self.tempdir.cleanup()

    def bundle(self, target):
        return self.root / f"raven-unsigned-{target}-{SHA}"

    def write_bundles(self):
        for target, (host, _, _, _) in verifier.TARGETS.items():
            path = self.bundle(target)
            path.mkdir(exist_ok=True)
            provenance = (
                f"commit={SHA}\ntarget={target}\nhost={host}\n"
                f"source_archive={DIST}.tar.gz\nstatus={STATUS}\n"
            )
            (path / "PROVENANCE.txt").write_text(provenance)
            files = dict(self.files[target])
            files["PROVENANCE.txt"] = provenance.encode()
            for name, content in files.items():
                (path / name).write_bytes(content)
            if target == "windows":
                deploy = path / "windeploy"
                deploy.mkdir(exist_ok=True)
                (deploy / "detached-sig-create.sh").write_bytes(b"test")
                (deploy / "win-codesign.cert").write_bytes(b"cert")
            self.rehash(target)

    def rehash(self, target):
        path = self.bundle(target)
        manifest = "".join(
            hashlib.sha256((path / name).read_bytes()).hexdigest() + "  " + name + "\n"
            for name in sorted((*self.files[target], "PROVENANCE.txt"))
        )
        (path / "SHA256SUMS").write_text(manifest)

    def verify(self):
        # The synthetic valid fixtures exercise structural checks. The real
        # unsigned installer and DMG are tested by 7-Zip in the CI verifier.
        with (mock.patch.object(verifier, "check_container"),
              mock.patch.object(verifier, "read_container_member",
                                side_effect=lambda path, name: self.installed[name]),
              mock.patch.object(verifier, "list_dmg_app_entries",
                                return_value=self.dmg_entries, create=True),
              mock.patch.object(verifier, "extract_dmg_app_file",
                                side_effect=lambda path, name, size: self.installed[name],
                                create=True)):
            return verifier.verify(self.root, SHA, DIST)

    def test_five_valid_bundles(self):
        result = self.verify()
        self.assertEqual({item["target"] for item in result["targets"]},
                         set(verifier.TARGETS))
        self.assertEqual(result["source_sha"], SHA)

    def test_missing_target_fails(self):
        self.bundle("osx").rename(self.root / "missing")
        with self.assertRaisesRegex(ValueError, "bundle directories"):
            self.verify()

    def test_wrong_sha_fails_even_with_updated_manifest(self):
        path = self.bundle("linux") / "PROVENANCE.txt"
        path.write_text(path.read_text().replace(SHA, "b" * 40))
        self.rehash("linux")
        with self.assertRaisesRegex(ValueError, "provenance"):
            self.verify()

    def test_modified_payload_fails_without_manifest_update(self):
        binary = self.bundle("linux") / (DIST + verifier.TARGETS["linux"][1][0])
        binary.write_bytes(binary.read_bytes() + b"tamper")
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.verify()

    def test_wrong_target_binary_fails_with_updated_manifest(self):
        name = DIST + verifier.TARGETS["aarch64"][1][0]
        self.files["aarch64"][name] = archive(
            [(f"{DIST}/bin/{binary}", elf(0x3E, 2)) for binary in
             ("ravend", "raven-cli", "raven-qt")]
        )
        (self.bundle("aarch64") / name).write_bytes(self.files["aarch64"][name])
        self.rehash("aarch64")
        with self.assertRaisesRegex(ValueError, "machine"):
            self.verify()

    def test_missing_binary_fails_with_updated_manifest(self):
        name = DIST + verifier.TARGETS["linux"][1][0]
        contents = archive([(f"{DIST}/bin/ravend", elf(0x3E, 2)),
                            (f"{DIST}/bin/raven-cli", elf(0x3E, 2))])
        self.files["linux"][name] = contents
        (self.bundle("linux") / name).write_bytes(contents)
        self.rehash("linux")
        with self.assertRaisesRegex(ValueError, "missing binary"):
            self.verify()

    def test_skeletal_elf_fails_with_updated_manifest(self):
        target = "linux"
        name = DIST + verifier.TARGETS[target][1][0]
        contents = archive([
            (f"{DIST}/bin/{binary}", elf(0x3E, 2)[:64])
            for binary in ("ravend", "raven-cli", "raven-qt")
        ])
        self.files[target][name] = contents
        (self.bundle(target) / name).write_bytes(contents)
        self.rehash(target)
        with self.assertRaisesRegex(ValueError, "small|program header"):
            self.verify()

    def test_padded_elf_without_program_headers_fails(self):
        target = "linux"
        name = DIST + verifier.TARGETS[target][1][0]
        broken = bytearray(elf(0x3E, 2))
        struct.pack_into("<H", broken, 56, 0)
        contents = archive([(f"{DIST}/bin/{binary}", bytes(broken))
                            for binary in ("ravend", "raven-cli", "raven-qt")])
        self.files[target][name] = contents
        (self.bundle(target) / name).write_bytes(contents)
        self.rehash(target)
        with self.assertRaisesRegex(ValueError, "program header"):
            self.verify()

    def test_padded_macho_without_load_commands_fails(self):
        target = "osx"
        name = DIST + verifier.TARGETS[target][1][0]
        broken = bytearray(macho())
        struct.pack_into("<I", broken, 16, 0)
        contents = archive([(f"{DIST}/bin/{binary}", bytes(broken))
                            for binary in ("ravend", "raven-cli", "raven-qt")])
        self.files[target][name] = contents
        (self.bundle(target) / name).write_bytes(contents)
        self.rehash(target)
        with self.assertRaisesRegex(ValueError, "load command"):
            self.verify()

    def test_padded_pe_without_executable_section_fails(self):
        target = "windows"
        name = DIST + verifier.TARGETS[target][1][0]
        broken = bytearray(pe(0x8664))
        struct.pack_into("<I", broken, 152 + 240 + 36, 0)
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as zipped:
            for binary in ("ravend.exe", "raven-cli.exe", "raven-qt.exe"):
                zipped.writestr(f"{DIST}/bin/{binary}", broken)
        self.files[target][name] = buffer.getvalue()
        (self.bundle(target) / name).write_bytes(buffer.getvalue())
        self.rehash(target)
        with self.assertRaisesRegex(ValueError, "executable section"):
            self.verify()

    def test_skeletal_windows_installer_fails(self):
        target = "windows"
        name = DIST + verifier.TARGETS[target][1][1]
        self.files[target][name] = pe(0x14C)[:256]
        (self.bundle(target) / name).write_bytes(self.files[target][name])
        self.rehash(target)
        with self.assertRaisesRegex(ValueError, "small|section"):
            self.verify()

    def test_dummy_dmg_fails(self):
        target = "osx"
        name = DIST + verifier.TARGETS[target][1][1]
        self.files[target][name] = bytes(512) + b"koly" + bytes(508)
        (self.bundle(target) / name).write_bytes(self.files[target][name])
        self.rehash(target)
        with self.assertRaisesRegex(ValueError, "small DMG"):
            self.verify()

    def test_archive_parser_rejects_synthetic_installers(self):
        for target, suffix_index, container_type in (
                ("windows", 1, "Nsis"), ("osx", 1, "Dmg")):
            with self.subTest(target=target):
                name = DIST + verifier.TARGETS[target][1][suffix_index]
                with self.assertRaisesRegex(ValueError, "invalid " + container_type):
                    verifier.check_container(self.bundle(target) / name,
                                             container_type)

    def test_mac_signing_app_must_match_primary(self):
        target = "osx"
        name = DIST + verifier.TARGETS[target][1][2]
        changed = bytearray(macho())
        changed[-1] = 1
        contents = archive([
            ("./dist/Raven-Qt.app/Contents/Info.plist", b"test"),
            ("./dist/Raven-Qt.app/Contents/MacOS/Raven-Qt", bytes(changed)),
            ("./dist/Raven-Qt.app/Contents/MacOS/raven-cli", macho()),
            ("./dist/Raven-Qt.app/Contents/MacOS/ravend", macho()),
        ])
        self.files[target][name] = contents
        (self.bundle(target) / name).write_bytes(contents)
        self.rehash(target)
        with self.assertRaisesRegex(ValueError, "signing app differs"):
            self.verify()

    def test_windows_installer_from_other_build_fails_with_updated_manifest(self):
        changed = bytearray(pe(0x8664))
        changed[0x300] = 1
        self.installed["daemon/ravend.exe"] = bytes(changed)
        with self.assertRaisesRegex(ValueError, "installer executable differs"):
            self.verify()

    def test_windows_debuglink_removal_preserves_installed_binary(self):
        installed = bytearray(pe(0x8664)[:0x10200])
        struct.pack_into("<H", installed, 150, 0x22E)
        primary = pe_with_debuglink(bytes(installed))
        verifier.compare_installed_pe(primary, bytes(installed), "ravend.exe")
        changed = bytearray(installed)
        struct.pack_into("<I", changed, 168, 0x2000)
        with self.assertRaisesRegex(ValueError, "installer executable differs"):
            verifier.compare_installed_pe(primary, bytes(changed), "ravend.exe")
        with self.assertRaisesRegex(ValueError, "overlay"):
            verifier.compare_installed_pe(primary, bytes(installed) + b"replacement", "ravend.exe")

    def test_windows_unsigned_payload_rejects_certificate_table(self):
        executable = bytearray(pe(0x8664))
        struct.pack_into("<II", executable, 152 + 144, 0x11000, 256)
        with self.assertRaisesRegex(ValueError, "certificate table"):
            verifier.compare_installed_pe(bytes(executable), bytes(executable),
                                          "ravend.exe")

    def test_ambiguous_installer_member_fails(self):
        listing = ("Header\n----------\n"
                   "Path = daemon/ravend.exe\nSize = 131072\n\n"
                   "Path = ./daemon/ravend.exe\nSize = 131072\n")
        completed = subprocess.CompletedProcess([], 0, listing, "")
        with mock.patch.object(verifier.subprocess, "run", return_value=completed):
            with self.assertRaisesRegex(ValueError, "ambiguous installer member"):
                verifier.read_container_member(Path("setup.exe"), "daemon/ravend.exe")

    def test_mac_dmg_from_other_build_fails_with_updated_manifest(self):
        changed = bytearray(macho())
        changed[-1] = 1
        self.installed["Raven-Qt.app/Contents/MacOS/Raven-Qt"] = bytes(changed)
        with self.assertRaisesRegex(ValueError, "DMG app differs"):
            self.verify()

    def test_mac_dmg_resource_substitution_fails(self):
        self.installed["Raven-Qt.app/Contents/Resources/qt.conf"] = b"Plugins=BadPlug\n"
        with self.assertRaisesRegex(ValueError, "DMG app differs from signing archive"):
            self.verify()

    def test_mac_dmg_extra_app_member_fails(self):
        name = "Raven-Qt.app/Contents/Resources/extra.conf"
        self.dmg_entries[name] = {"Path": name, "Folder": "-", "Size": "4",
                                  "Symbolic Link": ""}
        with self.assertRaisesRegex(ValueError, "DMG app inventory differs"):
            self.verify()

    def test_mac_dmg_case_alias_fails(self):
        listing = ("Header\n----------\n"
                   "Path = Raven-Qt.app\nFolder = +\n\n"
                   "Path = raven-qt.app/Contents/Info.plist\nFolder = -\nSize = 4\n")
        completed = subprocess.CompletedProcess([], 0, listing, "")
        with mock.patch.object(verifier.subprocess, "run", return_value=completed):
            with self.assertRaisesRegex(ValueError, "unsafe or ambiguous DMG app path"):
                verifier.list_dmg_app_entries(Path("app.dmg"))

    def test_windows_signing_bundle_requires_certificate(self):
        target = "windows"
        name = DIST + verifier.TARGETS[target][1][2]
        contents = archive([("./detached-sig-create.sh", b"test")])
        self.files[target][name] = contents
        (self.bundle(target) / name).write_bytes(contents)
        self.rehash(target)
        with self.assertRaisesRegex(ValueError, "signing bundle"):
            self.verify()

    def test_windows_deploy_directory_is_required(self):
        deploy = self.bundle("windows") / "windeploy"
        for file in deploy.iterdir():
            file.unlink()
        deploy.rmdir()
        with self.assertRaisesRegex(ValueError, "windeploy"):
            self.verify()

    def test_wrong_embedded_source_commit_fails_with_updated_manifest(self):
        source = archive([(f"{DIST}/README", b"source")], commit="b" * 40)
        for target in verifier.TARGETS:
            name = DIST + ".tar.gz"
            self.files[target][name] = source
            (self.bundle(target) / name).write_bytes(source)
            self.rehash(target)
        with self.assertRaisesRegex(ValueError, "source archive commit"):
            self.verify()

    def test_nonidentical_source_archives_fail(self):
        name = DIST + ".tar.gz"
        altered = archive([(f"{DIST}/README", b"different")], commit=SHA)
        self.files["osx"][name] = altered
        (self.bundle("osx") / name).write_bytes(altered)
        self.rehash("osx")
        with self.assertRaisesRegex(ValueError, "source archives differ"):
            self.verify()

    def test_forged_source_commit_header_fails_checkout_comparison(self):
        repo = self.root / "checkout"
        repo.mkdir()
        subprocess.run(["git", "-C", str(repo), "init", "-q"], check=True)
        (repo / "README").write_text("correct source\n")
        subprocess.run(["git", "-C", str(repo), "add", "README"], check=True)
        env = os.environ.copy()
        env.update({
            "GIT_AUTHOR_NAME": "ALENOC",
            "GIT_AUTHOR_EMAIL": "185200505+ALENOC@users.noreply.github.com",
            "GIT_COMMITTER_NAME": "ALENOC",
            "GIT_COMMITTER_EMAIL": "185200505+ALENOC@users.noreply.github.com",
        })
        subprocess.run(["git", "-C", str(repo), "commit", "-qm", "[FINDING-020] Fixture source tree"],
                       check=True, env=env)
        sha = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"],
                                      text=True).strip()
        source = self.root / "source.tar.gz"
        genuine = subprocess.check_output(
            ["git", "-C", str(repo), "archive", "--format=tar",
             f"--prefix={DIST}/", sha]
        )
        source.write_bytes(gzip.compress(genuine, mtime=0))
        verifier.check_source_matches_checkout(source, repo, sha, DIST)
        source.write_bytes(archive([(f"{DIST}/README", b"forged source")], commit=sha))
        self.assertEqual(verifier.source_commit(source), sha)
        with self.assertRaisesRegex(ValueError, "source archive content differs"):
            verifier.check_source_matches_checkout(source, repo, sha, DIST)

    def test_unsafe_tar_member_fails_with_updated_manifest(self):
        name = DIST + verifier.TARGETS["linux"][1][0]
        members = [(f"{DIST}/bin/{binary}", elf(0x3E, 2)) for binary in
                   ("ravend", "raven-cli", "raven-qt")]
        members.append(("../outside", b"escape"))
        self.files["linux"][name] = archive(members)
        (self.bundle("linux") / name).write_bytes(self.files["linux"][name])
        self.rehash("linux")
        with self.assertRaisesRegex(ValueError, "unsafe archive path"):
            self.verify()

    def test_unsafe_zip_member_fails_with_updated_manifest(self):
        name = DIST + verifier.TARGETS["windows"][1][0]
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as z:
            for binary in ("ravend.exe", "raven-cli.exe", "raven-qt.exe"):
                z.writestr(f"{DIST}/bin/{binary}", pe(0x8664))
            z.writestr("../outside", b"escape")
        self.files["windows"][name] = buffer.getvalue()
        (self.bundle("windows") / name).write_bytes(buffer.getvalue())
        self.rehash("windows")
        with self.assertRaisesRegex(ValueError, "unsafe archive path"):
            self.verify()

    def test_unsafe_tar_symlink_fails_with_updated_manifest(self):
        name = DIST + verifier.TARGETS["linux"][1][0]
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode="w") as tar:
            for binary in ("ravend", "raven-cli", "raven-qt"):
                entry = tarfile.TarInfo(f"{DIST}/bin/{binary}")
                data = elf(0x3E, 2)
                entry.size = len(data)
                tar.addfile(entry, io.BytesIO(data))
            link = tarfile.TarInfo(f"{DIST}/bin/escape")
            link.type = tarfile.SYMTYPE
            link.linkname = "../../../outside"
            tar.addfile(link)
        contents = gzip.compress(buffer.getvalue(), mtime=0)
        self.files["linux"][name] = contents
        (self.bundle("linux") / name).write_bytes(contents)
        self.rehash("linux")
        with self.assertRaisesRegex(ValueError, "unsafe archive path"):
            self.verify()

    def test_bad_sidecar_fails(self):
        name = DIST + verifier.TARGETS["linux"][1][0]
        (self.bundle("linux") / (name + ".sha256sum")).write_text("0" * 64 + "  " + name + "\n")
        with self.assertRaisesRegex(ValueError, "sidecar"):
            self.verify()

    def test_bad_dmg_fails_with_updated_manifest(self):
        name = DIST + verifier.TARGETS["osx"][1][1]
        self.files["osx"][name] = b"not a disk image"
        (self.bundle("osx") / name).write_bytes(self.files["osx"][name])
        self.rehash("osx")
        with self.assertRaisesRegex(ValueError, "DMG"):
            self.verify()


if __name__ == "__main__":
    unittest.main()
