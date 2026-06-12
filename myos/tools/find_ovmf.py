#!/usr/bin/env python3
"""Locate OVMF/edk2 UEFI firmware for QEMU.

Prints the CODE firmware path on the first stdout line. If a separate writable
VARS template is found, it is printed on the second line. Exits non-zero with a
clear message when nothing is found.
"""
import os
import shutil
import sys


def _qemu_share_dirs():
    """Directories that may hold QEMU's bundled edk2 firmware.

    Includes the install's own share/ (derived from the qemu executable) so the
    Windows winget build (C:\\Program Files\\qemu\\share\\...) is found without
    hard-coding the path."""
    dirs = []
    exe = shutil.which("qemu-system-x86_64") or shutil.which("qemu-system-x86_64.exe")
    if exe:
        base = os.path.dirname(os.path.abspath(exe))
        dirs += [os.path.join(base, "share"), base]
    dirs += [
        r"C:\Program Files\qemu\share",
        r"C:\Program Files\qemu",
    ]
    return dirs

# (code, vars-or-None) candidate pairs, in priority order.
CODE_CANDIDATES = [
    os.environ.get("OVMF_CODE"),
    "/opt/homebrew/share/qemu/edk2-x86_64-code.fd",
    "/usr/local/share/qemu/edk2-x86_64-code.fd",
    "/opt/homebrew/share/qemu/edk2-x86_64-code.4m.fd",
    "/usr/share/OVMF/OVMF_CODE.fd",
    "/usr/share/OVMF/OVMF_CODE_4M.fd",
    "/usr/share/edk2-ovmf/x64/OVMF_CODE.fd",
    "/usr/share/edk2/ovmf/OVMF_CODE.fd",
    "/usr/share/edk2/x64/OVMF_CODE.fd",
    "/usr/share/qemu/OVMF.fd",
    "/usr/share/ovmf/OVMF.fd",
    "/usr/share/OVMF/OVMF.fd",
] + [os.path.join(d, "edk2-x86_64-code.fd") for d in _qemu_share_dirs()]

VARS_CANDIDATES = [
    os.environ.get("OVMF_VARS"),
    "/opt/homebrew/share/qemu/edk2-i386-vars.fd",
    "/usr/local/share/qemu/edk2-i386-vars.fd",
    "/usr/share/OVMF/OVMF_VARS.fd",
    "/usr/share/OVMF/OVMF_VARS_4M.fd",
    "/usr/share/edk2-ovmf/x64/OVMF_VARS.fd",
    "/usr/share/edk2/ovmf/OVMF_VARS.fd",
] + [os.path.join(d, "edk2-i386-vars.fd") for d in _qemu_share_dirs()]


def find_first(candidates):
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    return None


def find_ovmf():
    """Return (code_path, vars_path_or_None) or (None, None)."""
    code = find_first(CODE_CANDIDATES)
    vars_ = find_first(VARS_CANDIDATES)
    return code, vars_


def main():
    code, vars_ = find_ovmf()
    if not code:
        sys.stderr.write(
            "\n[DEP MISSING] OVMF/edk2 UEFI firmware not found.\n"
            "  Install QEMU (it ships edk2 firmware):\n"
            "    macOS: brew install qemu\n"
            "    Debian/Ubuntu: apt-get install ovmf\n"
            "    Fedora: dnf install edk2-ovmf\n"
            "  Or set OVMF_CODE=/path/to/OVMF_CODE.fd (and optionally OVMF_VARS).\n\n"
        )
        return 1
    print(code)
    if vars_:
        print(vars_)
    return 0


if __name__ == "__main__":
    sys.exit(main())
