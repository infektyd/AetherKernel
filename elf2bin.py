#!/usr/bin/env python3
"""
Convert an ELF file to a raw binary by extracting PT_LOAD segments.
Each segment's file data is placed at (p_paddr - lowest_paddr) in the output.
Gaps between segments are zero-padded.
"""
import struct
import sys
import os

def elf2bin(elf_path, bin_path):
    with open(elf_path, 'rb') as f:
        data = f.read()

    # Parse ELF header
    magic = data[0:4]
    if magic != b'\x7fELF':
        raise ValueError("Not an ELF file")

    ei_class = data[4]  # 1=32bit, 2=64bit
    ei_data = data[5]   # 1=little-endian, 2=big-endian

    endian = '<' if ei_data == 1 else '>'

    if ei_class == 1:
        # ELF32
        (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff,
         e_flags, e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum,
         e_shstrndx) = struct.unpack_from(endian + 'HHIIIIIHHHHHH', data, 16)
        ph_fmt = endian + 'IIIIIIII'  # p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align
        ph_size = 32
    else:
        # ELF64
        (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff,
         e_flags, e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum,
         e_shstrndx) = struct.unpack_from(endian + 'HHIQQQIHHHHHH', data, 16)
        ph_fmt = endian + 'IIQQQQQQ'  # p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align
        ph_size = 56

    PT_LOAD = 1

    segments = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        if ei_class == 1:
            (p_type, p_offset, p_vaddr, p_paddr,
             p_filesz, p_memsz, p_flags, p_align) = struct.unpack_from(ph_fmt, data, off)
        else:
            (p_type, p_flags, p_offset, p_vaddr, p_paddr,
             p_filesz, p_memsz, p_align) = struct.unpack_from(ph_fmt, data, off)

        if p_type == PT_LOAD and p_filesz > 0:
            segments.append((p_paddr, p_offset, p_filesz))

    if not segments:
        raise ValueError("No PT_LOAD segments with file data found")

    # Sort by physical address
    segments.sort(key=lambda s: s[0])

    lowest_paddr = segments[0][0]
    highest_end = max(paddr + filesz for paddr, _, filesz in segments)
    total_size = highest_end - lowest_paddr

    print(f"  ELF class: {'64-bit' if ei_class == 2 else '32-bit'}, "
          f"endian: {'little' if ei_data == 1 else 'big'}")
    print(f"  Lowest paddr: 0x{lowest_paddr:08x}")
    print(f"  Output size: {total_size} bytes (0x{total_size:x})")
    print(f"  Segments: {len(segments)}")
    for paddr, foff, filesz in segments:
        print(f"    paddr=0x{paddr:08x} foff=0x{foff:08x} filesz=0x{filesz:x}")

    # Build output buffer (zero-padded)
    out = bytearray(total_size)
    for paddr, foff, filesz in segments:
        dest = paddr - lowest_paddr
        out[dest:dest + filesz] = data[foff:foff + filesz]

    with open(bin_path, 'wb') as f:
        f.write(out)

    print(f"  Written: {bin_path} ({len(out)} bytes)")


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.elf> <output.bin>")
        sys.exit(1)
    elf2bin(sys.argv[1], sys.argv[2])
