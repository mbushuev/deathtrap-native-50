#!/usr/bin/env python3
"""Small reproducible PE/x86 helper for the supported Dungeon.dll.

The runtime patch is tied to one verified Steam binary.  This tool keeps the
static part of camera reverse engineering repeatable: it disassembles an RVA
and can find instructions that reference an image address.  It never modifies
the game binary.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import sys

try:
    import pefile
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    from capstone.x86 import X86_OP_IMM, X86_OP_MEM
except ImportError as error:
    raise SystemExit(
        "Install the analysis dependencies with: "
        "python -m pip install pefile capstone"
    ) from error


EXPECTED_SHA256 = (
    "95FE9CE0FFF387F00704548F152E4340815213FCB3833DBE1B5C42871E7D2E56"
)


def integer(text: str) -> int:
    return int(text, 0)


def load_image(path: Path):
    digest = hashlib.sha256(path.read_bytes()).hexdigest().upper()
    if digest != EXPECTED_SHA256:
        raise SystemExit(
            f"Unsupported Dungeon.dll SHA-256: {digest}\n"
            f"Expected: {EXPECTED_SHA256}"
        )
    image = pefile.PE(str(path), fast_load=True)
    if image.OPTIONAL_HEADER.Magic != 0x10B:
        raise SystemExit("Dungeon.dll is not a 32-bit PE image")
    return image


def disassembler() -> Cs:
    engine = Cs(CS_ARCH_X86, CS_MODE_32)
    engine.detail = True
    return engine


def print_instruction(instruction, image_base: int) -> None:
    print(
        f"{instruction.address - image_base:08X}  "
        f"{instruction.mnemonic:9} {instruction.op_str}"
    )


def disassemble_range(image, rva: int, size: int) -> None:
    base = image.OPTIONAL_HEADER.ImageBase
    code = image.get_data(rva, size)
    for instruction in disassembler().disasm(code, base + rva):
        print_instruction(instruction, base)


def text_section(image):
    for section in image.sections:
        if section.Name.rstrip(b"\0") == b".text":
            return section
    raise SystemExit("PE image has no .text section")


def references_target(instruction, target: int) -> bool:
    for operand in instruction.operands:
        if operand.type == X86_OP_IMM and operand.imm == target:
            return True
        if (
            operand.type == X86_OP_MEM
            and operand.mem.base == 0
            and operand.mem.index == 0
            and operand.mem.disp == target
        ):
            return True
    return False


def find_references(image, target_rva: int) -> None:
    section = text_section(image)
    base = image.OPTIONAL_HEADER.ImageBase
    start = section.VirtualAddress
    code = image.get_data(start, section.Misc_VirtualSize)
    target = base + target_rva
    for instruction in disassembler().disasm(code, base + start):
        if references_target(instruction, target):
            print_instruction(instruction, base)

    # Capstone's linear sweep stops at embedded data or undecodable bytes in
    # some old MSVC functions.  Direct x86 calls are unambiguous enough to
    # recover independently, so scan E8 rel32 sites as a second pass.  This is
    # intentionally read-only and may print the same call already found above.
    for offset in range(0, max(0, len(code) - 4)):
        if code[offset] != 0xE8:
            continue
        displacement = struct.unpack_from("<i", code, offset + 1)[0]
        call_rva = start + offset
        if call_rva + 5 + displacement == target_rva:
            print(f"{call_rva:08X}  call      0x{base + target_rva:08x}")


def references_displacement(instruction, displacement: int) -> bool:
    return any(
        operand.type == X86_OP_MEM and operand.mem.disp == displacement
        for operand in instruction.operands
    )


def find_displacements(image, displacement: int) -> None:
    """Find structure-field accesses such as ``[esi + 0x1ac]``."""
    section = text_section(image)
    base = image.OPTIONAL_HEADER.ImageBase
    start = section.VirtualAddress
    code = image.get_data(start, section.Misc_VirtualSize)
    for instruction in disassembler().disasm(code, base + start):
        if references_displacement(instruction, displacement):
            print_instruction(instruction, base)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path, help="path to the supported Dungeon.dll")
    parser.add_argument("--rva", type=integer, default=0x3860)
    parser.add_argument("--size", type=integer, default=0x80)
    parser.add_argument(
        "--xrefs",
        type=integer,
        help="find .text references to this RVA instead of disassembling",
    )
    parser.add_argument(
        "--disp",
        type=integer,
        help="find .text memory operands using this structure displacement",
    )
    arguments = parser.parse_args()
    image = load_image(arguments.dll)
    if arguments.xrefs is not None:
        find_references(image, arguments.xrefs)
    elif arguments.disp is not None:
        find_displacements(image, arguments.disp)
    else:
        disassemble_range(image, arguments.rva, arguments.size)
    return 0


if __name__ == "__main__":
    sys.exit(main())
