#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from __future__ import annotations

import argparse
import pathlib
import struct


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert SPIR-V binary to C++ header array.")
    parser.add_argument("--input", required=True, help="Input .spv file")
    parser.add_argument("--output", required=True, help="Output .hpp file")
    parser.add_argument("--symbol", required=True, help="C++ symbol name")
    parser.add_argument("--namespace", default="vr::text::generated", help="C++ namespace")
    args = parser.parse_args()

    input_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)

    blob = input_path.read_bytes()
    if len(blob) % 4 != 0:
        raise RuntimeError(f"SPIR-V byte size must be 4-byte aligned: {input_path} ({len(blob)} bytes)")

    word_count = len(blob) // 4
    words = struct.unpack("<" + ("I" * word_count), blob)

    output_path.parent.mkdir(parents=True, exist_ok=True)

    with output_path.open("w", encoding="utf-8", newline="\n") as f:
        f.write("#pragma once\n\n")
        f.write("#include <cstdint>\n")
        f.write("#include <cstddef>\n\n")

        namespaces = [ns for ns in args.namespace.split("::") if ns]
        for ns in namespaces:
            f.write(f"namespace {ns} {{\n")
        if namespaces:
            f.write("\n")

        f.write(f"inline constexpr std::uint32_t {args.symbol}[] = {{\n")
        for index, word in enumerate(words):
            if index % 8 == 0:
                f.write("    ")
            f.write(f"0x{word:08X}U")
            if index + 1 != len(words):
                f.write(", ")
            if index % 8 == 7 or index + 1 == len(words):
                f.write("\n")
        f.write("};\n")
        f.write(
            f"inline constexpr std::size_t {args.symbol}_word_count = sizeof({args.symbol}) / sizeof({args.symbol}[0]);\n"
        )

        if namespaces:
            f.write("\n")
        for ns in reversed(namespaces):
            f.write(f"}} // namespace {ns}\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

