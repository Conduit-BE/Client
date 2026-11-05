#!/usr/bin/env python3
"""Prepare an analysis-only PE copy and original .pdata-bounded Ghidra selections.

Never execute the output. This disables automatic exception-directory markup;
all section bytes, including .text and .pdata, are retained. No game offsets or
names are embedded here. Each requested RVA must be an original unwind entry.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct


def prepare(source, output, requested):
    if source.resolve() == output.resolve():
        raise ValueError("Analysis output must differ from the original")
    manifest_path = output.with_suffix(".manifest.json")
    selections_path = output.with_suffix(".functions.tsv")
    for path in (output, manifest_path, selections_path):
        if path.exists():
            raise ValueError(f"Refusing to overwrite {path}")
    original = source.read_bytes()
    image = bytearray(original)
    if image[:2] != b"MZ":
        raise ValueError("Expected DOS/PE header")
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    if image[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Expected PE signature")
    machine, count = struct.unpack_from("<HH", image, pe + 4)
    optional_size = struct.unpack_from("<H", image, pe + 20)[0]
    optional = pe + 24
    if machine != 0x8664 or struct.unpack_from("<H", image, optional)[0] != 0x20B:
        raise ValueError("Only x64 PE32+ is supported")
    if struct.unpack_from("<I", image, optional + 108)[0] <= 3:
        raise ValueError("Missing exception data directory")
    image_base = struct.unpack_from("<Q", image, optional + 24)[0]
    directory_offset = optional + 112 + 3 * 8
    directory_rva, directory_size = struct.unpack_from("<II", image, directory_offset)
    sections = []
    for index in range(count):
        offset = optional + optional_size + index * 40
        _, rva, size, raw = struct.unpack_from("<IIII", image, offset + 8)
        if raw + size > len(image):
            raise ValueError("Section extends beyond the input")
        sections.append((rva, size, raw))
    matches = [raw + directory_rva - rva for rva, size, raw in sections
               if rva <= directory_rva and directory_rva + directory_size <= rva + size]
    if len(matches) != 1 or directory_size % 12:
        raise ValueError("Exception directory is not one complete x64 unwind table")
    offset = matches[0]
    selections = []
    wanted = set(requested)
    for start, end, unwind in struct.iter_unpack("<III", image[offset:offset + directory_size]):
        if start in wanted:
            if start >= end:
                raise ValueError("Invalid selected unwind interval")
            selections.append({"rva": hex(start), "end_rva_exclusive": hex(end),
                               "unwind_rva": hex(unwind)})
    if {int(item["rva"], 16) for item in selections} != wanted:
        raise ValueError("Every selected RVA must exactly match an original unwind entry")
    before = bytes(image[directory_offset:directory_offset + 8])
    image[directory_offset:directory_offset + 8] = bytes(8)
    manifest = {
        "source": str(source.resolve()), "source_sha256": hashlib.sha256(original).hexdigest(),
        "derived": str(output.resolve()), "derived_sha256": hashlib.sha256(image).hexdigest(),
        "image_base": hex(image_base), "patch_file_offset": hex(directory_offset),
        "before_hex": before.hex(), "after_hex": bytes(8).hex(),
        "original_unwind_entries": directory_size // 12, "selections": selections,
        "scope": "Static analysis only. No instruction/section bytes changed. Never execute output."
    }
    output.write_bytes(image)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    selections_path.write_text("".join(
        f"{image_base + int(item['rva'], 16):x}\t"
        f"{image_base + int(item['end_rva_exclusive'], 16):x}\n" for item in selections))
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--function-rva", action="append", required=True,
                        type=lambda value: int(value, 0))
    options = parser.parse_args()
    try:
        prepare(options.source, options.output, options.function_rva)
    except (ValueError, OSError, struct.error) as error:
        parser.exit(1, f"{error}\n")
