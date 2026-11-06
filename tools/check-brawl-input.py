#!/usr/bin/env python3
"""Read-only validation of private, extracted Brawl inputs before prelinking.

The checked-in layout pins each original REL's runtime identity (CRC32 over its
fixed image), not a cryptographic digest of the complete file. No verified raw
retail main.dol hash is available in this repository: its digest is reported,
and its container/entry point are checked, without claiming byte identity.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib


def require(condition, message):
    if not condition:
        raise ValueError(message)


def u32(data, offset):
    require(offset + 4 <= len(data), "truncated binary header")
    return struct.unpack_from(">I", data, offset)[0]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def check_dol(data):
    require(len(data) >= 0x100, "main.dol header is truncated")
    entry = u32(data, 0xE0)
    require(entry == 0x8000403C, "main.dol has an unsupported entry point")
    file_ranges, memory_ranges, text_ranges = [], [], []
    for i in range(18):
        offset, address, size = u32(data, i * 4), u32(data, 0x48 + i * 4), u32(data, 0x90 + i * 4)
        if not size:
            continue
        require(offset >= 0x100 and offset + size <= len(data), "main.dol section extends outside the file")
        require(0x80000000 <= address < address + size <= 0x81800000, "main.dol section extends outside MEM1")
        file_ranges.append((offset, offset + size))
        memory_ranges.append((address, address + size))
        if i < 7:
            text_ranges.append((address, address + size))
    for ranges in (file_ranges, memory_ranges):
        ranges.sort()
        require(all(a[1] <= b[0] for a, b in zip(ranges, ranges[1:])), "main.dol has overlapping sections")
    require(any(start <= entry < end for start, end in text_ranges), "main.dol entry is outside its text sections")
    return len(file_ranges)


def validate(extracted, layout_path):
    layout_bytes = layout_path.read_bytes()
    layout = json.loads(layout_bytes)
    require(len(layout) == 126, "reference module layout must contain exactly 126 modules")
    boot = (extracted / "sys/boot.bin").read_bytes()
    require(len(boot) >= 0x20, "sys/boot.bin is truncated")
    require(boot[:6] == b"RSBE01" and boot[6] == 0 and boot[7] == 1,
            "disc must be RSBE01, disc 0, revision 1")
    require(u32(boot, 0x18) == 0x5D1C9EA3, "sys/boot.bin has an invalid Wii magic")
    dol = (extracted / "sys/main.dol").read_bytes()
    section_count = check_dol(dol)
    module_dir = extracted / "files/module"
    modules = {p.stem: p for p in module_dir.glob("*.rel")}
    require(set(modules) == set(layout), "original REL names/count differ from the 126-module reference corpus")
    corpus = hashlib.sha256()
    seen_ids = set()
    for name in sorted(layout):
        expected = layout[name]
        raw = modules[name].read_bytes()
        require(len(raw) >= 0x4C, f"{name}: truncated REL")
        module_id, version = u32(raw, 0), u32(raw, 0x1C)
        require(module_id == expected["id"] and module_id not in seen_ids, f"{name}: unexpected/duplicate module ID")
        seen_ids.add(module_id)
        require(version == 3, f"{name}: unsupported REL version")
        fixed = u32(raw, 0x48)
        require(fixed == int(expected["fix_size"], 16) and 0x4C <= fixed <= len(raw), f"{name}: fixed-image size differs")
        require(u32(raw, 0x20) == int(expected["bss_size"], 16), f"{name}: BSS size differs")
        crc = zlib.crc32(raw[0x4C:fixed]) & 0xFFFFFFFF
        require(crc == int(expected["crc32"], 16), f"{name}: original fixed-image CRC32 differs")
        count, table = u32(raw, 0x0C), u32(raw, 0x10)
        require(0 < count <= 255 and 0x4C <= table and table + count * 8 <= fixed,
                f"{name}: invalid section table")
        for index in range(count):
            offset, size = u32(raw, table + index * 8) & ~1, u32(raw, table + index * 8 + 4)
            require(not offset or offset + size <= fixed, f"{name}: section extends outside fixed image")
        # Full-file digests are recorded even though the reference only pins the
        # fixed image. No game bytes, output paths or executable files are emitted.
        corpus.update(name.encode("ascii") + b"\0" + hashlib.sha256(raw).digest())
    return {
        "disc_id": "RSBE01", "revision": 1, "modules_checked": len(modules),
        "layout_sha256": digest(layout_bytes), "boot_sha256": digest(boot),
        "main_dol_sha256": digest(dol), "main_dol_sections": section_count,
        "main_dol_verification": "structure-and-entry-only; no trusted raw DOL digest is pinned",
        "rel_fixed_images": "all reference CRC32, IDs, fixed-image and BSS sizes match",
        "rel_full_file_corpus_sha256": corpus.hexdigest(),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("extracted", type=Path, help="private extracted DATA directory containing sys/ and files/")
    parser.add_argument("--layout", type=Path, default=Path(__file__).resolve().parents[1] /
                        "wiicompiled/projects/brawl/module_layout.json", help="unmodified reference layout")
    args = parser.parse_args()
    try:
        print(json.dumps(validate(args.extracted, args.layout), indent=2))
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        print(f"Brawl input validation FAILED: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
