"""Check that compiled production shaders use native heaps, without set/binding mappings."""

import argparse
from pathlib import Path
import struct
import sys


def check(path, sample_shading=False):
    blob = path.read_bytes()
    if len(blob) < 20 or len(blob) % 4:
        raise ValueError(f"{path}: invalid SPIR-V size")
    words = struct.unpack(f"<{len(blob) // 4}I", blob)
    if words[0] != 0x07230203:
        raise ValueError(f"{path}: invalid SPIR-V magic")
    offset = 5
    heap_extension = False
    sample_ids = set()
    loaded_ids = set()
    sample_rate_capability = False
    while offset < len(words):
        count, opcode = words[offset] >> 16, words[offset] & 0xFFFF
        if count == 0 or offset + count > len(words):
            raise ValueError(f"{path}: malformed instruction at word {offset}")
        args = words[offset + 1:offset + count]
        # OpDecorate: Binding (33) and DescriptorSet (34) require legacy mappings.
        if opcode == 71 and len(args) >= 2 and args[1] in (33, 34):
            raise ValueError(f"{path}: legacy descriptor decoration on ID {args[0]}")
        if opcode == 17 and args == (35,):  # OpCapability SampleRateShading
            sample_rate_capability = True
        if opcode == 71 and len(args) >= 3 and args[1:3] == (11, 18):
            sample_ids.add(args[0])  # OpDecorate BuiltIn SampleId
        if opcode == 61 and len(args) >= 3:  # OpLoad
            loaded_ids.add(args[2])
        if opcode == 10:  # OpExtension
            extension = struct.pack(f"<{len(args)}I", *args).split(b"\0", 1)[0]
            heap_extension |= extension == b"SPV_EXT_descriptor_heap"
        offset += count
    if not heap_extension:
        raise ValueError(f"{path}: native descriptor heap extension is missing")
    if sample_shading:
        if not sample_rate_capability or not sample_ids.intersection(loaded_ids):
            raise ValueError(f"{path}: full sample shading requires a static SampleId load")
        print(f"PASS: {path.name} retains SampleRateShading and a SampleId load")
    print(f"PASS: {path.name} uses native heaps without descriptor set mappings")


if __name__ == "__main__":
    try:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("--sample-shading", action="append", type=Path, default=[])
        parser.add_argument("shaders", nargs="+", type=Path)
        args = parser.parse_args()
        for path in args.shaders:
            check(path, sample_shading=path in args.sample_shading)
    except (OSError, ValueError, struct.error) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
