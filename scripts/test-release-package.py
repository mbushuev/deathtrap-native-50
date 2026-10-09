#!/usr/bin/env python3
"""Check the downloadable ZIP without silently repairing Windows path names."""

import hashlib
from pathlib import PurePosixPath
import sys
import zipfile


REQUIRED_FILES = {
    "INSTALL-DECK.sh", "INSTALL.cmd", "install.ps1", "README.txt",
    "CHANGELOG.txt", "LICENSE.txt", "THIRD_PARTY_NOTICES.txt", "SHA256SUMS.txt",
    "payload/DINPUT.dll", "payload/deathtrap_native.ini", "payload/keys.cfg",
    "payload/dxwrapper.ini", "payload/dgVoodoo.conf",
    "payload/dxwrapper/DDraw.dll", "payload/dxwrapper/dxwrapper.dll",
    "payload/dgVoodoo/D3D9.dll", "payload/dgVoodoo/D3DImm.dll",
}


def verify_archive(path):
    with zipfile.ZipFile(path) as archive:
        # ZipInfo.filename itself repairs backslashes on Windows. Inspect the
        # unmodified stored name so this regression check works on both hosts.
        names = [entry.orig_filename for entry in archive.infolist()]
        if len(set(names)) != len(names):
            raise ValueError("duplicate ZIP entry names")
        for name in names:
            parts = name.split("/")
            if ("\\" in name or ":" in name or "\0" in name or
                    PurePosixPath(name).is_absolute() or
                    any(part in ("", ".", "..") for part in parts)):
                raise ValueError(f"non-portable ZIP entry: {name}")
        missing = REQUIRED_FILES - set(names)
        if missing:
            raise ValueError(f"required ZIP files missing: {sorted(missing)}")

        manifest = {}
        for line in archive.read("SHA256SUMS.txt").decode("utf-8").splitlines():
            digest, separator, name = line.partition("  ")
            if (not separator or len(digest) != 64 or
                    any(char not in "0123456789abcdef" for char in digest) or
                    name in manifest):
                raise ValueError(f"invalid checksum manifest line: {line}")
            manifest[name] = digest
        if set(manifest) != set(names) - {"SHA256SUMS.txt"}:
            raise ValueError("checksum manifest does not cover exactly the ZIP payload")
        for name, expected in manifest.items():
            if hashlib.sha256(archive.read(name)).hexdigest() != expected:
                raise ValueError(f"checksum mismatch: {name}")
        if b"\r" in archive.read("INSTALL-DECK.sh"):
            raise ValueError("INSTALL-DECK.sh must use LF line endings")
        if b"\r" in archive.read("SHA256SUMS.txt"):
            raise ValueError("SHA256SUMS.txt must use LF line endings")
    print("Release ZIP paths, required files and checksums passed.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: python3 scripts/test-release-package.py RELEASE.zip")
    try:
        verify_archive(sys.argv[1])
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        sys.exit(f"Release ZIP verification failed: {error}")
