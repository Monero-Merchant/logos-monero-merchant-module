#!/usr/bin/env python3
"""Check every packaged 64-bit Android .so for 16 KiB ELF/RELRO/ZIP alignment."""
import argparse
from pathlib import Path
import struct
import sys
import zipfile

PAGE = 16384


def elf_errors(data):
    if data[:6] != b"\x7fELF\x02\x01":
        return ["expected a little-endian ELF64 library"]
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    offset, entry_size, count = header[5], header[9], header[10]
    loads, relros, errors = [], [], []
    for index in range(count):
        kind, flags, file_offset, address, _, file_size, size, alignment = struct.unpack_from(
            "<IIQQQQQQ", data, offset + index * entry_size)
        if kind == 1:  # PT_LOAD
            loads.append((address, size, flags))
            if alignment < PAGE or alignment & (alignment - 1):
                errors.append(f"LOAD alignment {alignment:#x} is below 16 KiB or invalid")
            if (address - file_offset) % PAGE:
                errors.append("LOAD address and file offset are not congruent modulo 16 KiB")
        elif kind == 0x6474E552:  # PT_GNU_RELRO
            relros.append((address, size))
    if not loads:
        errors.append("no LOAD segments")
    for address, size in relros:
        end = address + size
        if end % PAGE == 0:
            continue
        if any(flags & 2 and start <= address < start + length and start + length > end
               for start, length, flags in loads):
            errors.append("unaligned RELRO end shares a LOAD segment with trailing writable data")
    return errors


def check(apk):
    failed, checked = False, 0
    with zipfile.ZipFile(apk) as archive, apk.open("rb") as raw:
        for entry in archive.infolist():
            if not entry.filename.startswith(("lib/arm64-v8a/", "lib/x86_64/")) or not entry.filename.endswith(".so"):
                continue
            checked += 1
            errors = elf_errors(archive.read(entry))
            if entry.compress_type == zipfile.ZIP_STORED:
                raw.seek(entry.header_offset)
                local = struct.unpack("<IHHHHHIIIHH", raw.read(30))
                data_offset = entry.header_offset + 30 + local[-2] + local[-1]
                if data_offset % PAGE:
                    errors.append(f"uncompressed ZIP data offset {data_offset:#x} is not 16 KiB aligned")
            if errors:
                failed = True
                print(f"FAIL {entry.filename}: {'; '.join(dict.fromkeys(errors))}")
            else:
                print(f"PASS {entry.filename}")
    if not checked:
        print("FAIL: no supported 64-bit native libraries found")
        return 1
    print(f"Checked {checked} libraries: {'FAILED' if failed else '16 KiB alignment verified'}")
    return int(failed)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    sys.exit(check(parser.parse_args().apk))
