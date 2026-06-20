#!/usr/bin/env python3
"""Convert a binary file to a C uint8_t array header."""
import sys, os

def main():
    if len(sys.argv) != 4:
        print("Usage: bin2h.py <input.bin> <output.h> <array_name>")
        sys.exit(1)

    src, dst, name = sys.argv[1], sys.argv[2], sys.argv[3]

    with open(src, "rb") as f:
        data = f.read()

    lines = [
        "#pragma once",
        "#include <stdint.h>",
        f"/* Auto-generated from {os.path.basename(src)} — do not edit */",
        f"static const uint8_t {name}[] = {{",
    ]

    chunk_size = 16
    for i in range(0, len(data), chunk_size):
        chunk = data[i:i + chunk_size]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hex_vals},")

    lines += [
        "};",
        f"static const uint32_t {name}_len = {len(data)}U;",
    ]

    with open(dst, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"Generated {dst}: {len(data)} bytes")

if __name__ == "__main__":
    main()
