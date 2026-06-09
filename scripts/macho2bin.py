#! /usr/bin/env -S uv run --script

# /// script
# dependencies = [
#   "macholib==1.16.3",
# ]
# ///

# This source file is part of the Swift open source project
#
# Copyright (c) 2023 Apple Inc. and the Swift project authors.
# Licensed under Apache License v2.0 with Runtime Library Exception
#
# See https://swift.org/LICENSE.txt for license information

#
# macho2bin -- Converts a statically-linked executable Mach-O into a flat "BIN" file
# suitable for flashing as a single contiguous blob onto some embedded devices. Note
# that this format assumes the embedded device can boot from a state where the entire
# firmware (all segments) are flashed contigously into one smalle address range. This
# is true for e.g. the STM32F746 devices if we place the vector table at 0x00200000,
# and code and data right after it, as the vector table also contains a pointer to the
# initial PC. This setup might not work for other devices.
#
# Usage:
#   $ macho2bin.py <input> <output> --base-address <base-address> --segments
#       <segment-list>
#
# Example:
#   $ macho2bin.py ./blink ./blink.bin --base-address 0x00200000 --segments
#       '__TEXT,__DATA,__VECTORS'
#
# Requirements and notes:
#   * The output BIN file is a flat contiguous representation of the segments
#     (--segments) based on their VM addresses.
#   * The BIN file's first byte corresponds to the specified base address
#     (--base-address).
#   * Any gaps between segments are filled with zero bytes.
#   * Because of that, you want the input Mach-O to have all segments "close", and not
#     have gaps.
#

import argparse
import os

from macholib import MachO
from macholib import mach_o

IGNORED_SEGMENTS = {"__LINKEDIT", "__PAGEZERO"}


def _parse_hex(value):
    try:
        return int(value, 16)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid hex value: {value}") from exc


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--base-address", required=True, type=_parse_hex)
    parser.add_argument("--segments", required=True)
    parser.add_argument("--max-end-address", type=_parse_hex)
    args = parser.parse_args()
    args.segments = args.segments.split(",")

    segments = []
    found_segments = set()
    all_segments = set()
    macho = MachO.MachO(args.input)
    assert len(macho.headers) == 1
    mh = macho.headers[0]
    for command in mh.commands:
        if isinstance(command[1], (mach_o.segment_command, mach_o.segment_command_64)):
            (_, segment, sections) = command
            segname = segment.segname.decode().strip("\0")
            all_segments.add(segname)
            if segname not in args.segments:
                continue
            found_segments.add(segname)

            with open(args.input, "rb") as f:
                f.seek(mh.offset + segment.fileoff)
                data = f.read(segment.filesize)
                segments.append(
                    {"vmaddr": segment.vmaddr, "data": data, "name": segname}
                )

    missing = [segname for segname in args.segments if segname not in found_segments]
    if missing:
        parser.error(f"requested segment(s) not found: {', '.join(missing)}")

    unexpected = sorted(all_segments - set(args.segments) - IGNORED_SEGMENTS)
    if unexpected:
        parser.error(
            "unhandled Mach-O segment(s): "
            + ", ".join(unexpected)
            + "; add them to --segments or IGNORED_SEGMENTS"
        )

    segments = sorted(segments, key=lambda x: x["vmaddr"])

    if not segments:
        parser.error("no output segments selected")

    if segments[0]["vmaddr"] != args.base_address:
        parser.error(
            f"first segment's vmaddr 0x{segments[0]['vmaddr']:08x} does not match the"
            f" passed --base-address 0x{args.base_address:08x}"
        )

    projected_end = max(segment["vmaddr"] + len(segment["data"]) for segment in segments)
    if args.max_end_address is not None and projected_end > args.max_end_address:
        parser.error(
            f"image end 0x{projected_end:08x} exceeds --max-end-address "
            f"0x{args.max_end_address:08x}"
        )

    if os.path.exists(args.output):
        os.unlink(args.output)

    vmaddr = segments[0]["vmaddr"]
    with open(args.output, "wb") as f:
        for segment in segments:
            gap = segment["vmaddr"] - vmaddr
            if gap < 0:
                parser.error(
                    f"segment {segment['name']} overlaps previous output at "
                    f"vmaddr 0x{segment['vmaddr']:08x}"
                )
            if gap != 0:
                print(
                    f"Writing gap of size {gap} (0x{gap:0x}) at vmaddr 0x{vmaddr:08x}"
                )
                f.write(b"\0" * gap)
            vmaddr = segment["vmaddr"]
            print(
                f"Writing segment {segment['name']} size"
                f" {len(segment['data'])} (0x{len(segment['data']):x}) at vmaddr"
                f" 0x{vmaddr:08x}"
            )
            f.write(segment["data"])
            vmaddr = segment["vmaddr"] + len(segment["data"])

    print(f"Produced {args.output} with {vmaddr - args.base_address} bytes")


if __name__ == "__main__":
    main()
