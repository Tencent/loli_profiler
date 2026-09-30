"""Independently parse capture wire data and reconcile every CLI diff node.

Uses Python mmap/struct, without LoliCore or legacy tree/hash code. Each full
library-qualified path is accumulated in dictionaries. Checks exact inclusive
and self bytes/counts, zero-net ancestors, reverse signs, and identical inputs.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import mmap
from pathlib import Path
import struct
import subprocess


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def number(self, fmt=">I"):
        value = struct.unpack_from(fmt, self.data, self.pos)[0]
        self.pos += struct.calcsize(fmt)
        return value

    def blob(self):
        size = self.number()
        if size == 0xFFFFFFFF:
            return b""
        start = self.pos
        self.pos += size
        if self.pos > len(self.data):
            raise ValueError("Truncated capture")
        return self.data[start:self.pos]

    def text(self):
        return self.blob().decode("utf-16-be")


def read_capture(path):
    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        reader = Reader(data)
        assert reader.number() == 0xA4B3C2D1 and reader.number() == 106
        reader.number()  # max meminfo
        for _ in range(reader.number()):
            points = reader.number()
            reader.pos += points * 16
        libraries = {}
        for _ in range(reader.number()):
            key = reader.number()
            libraries[key] = reader.text()
        record_count = reader.number()
        record_start = reader.pos
        for _ in range(record_count):
            length = reader.number()
            reader.pos += length + 32
        stacks = {}
        for _ in range(reader.number()):
            uuid = reader.blob()
            frames = []
            for _ in range(reader.number()):
                frames.append((reader.number(), reader.number(">Q")))
            stacks[uuid] = frames
        symbols = {}
        for _ in range(reader.number()):
            library = reader.text()
            table = {}
            for _ in range(reader.number()):
                address = reader.number(">Q")
                table[address] = reader.text()
            symbols[library] = table
        frees = {}
        for _ in range(reader.number()):
            address, seq = reader.number(">Q"), reader.number()
            frees[address] = max(seq, frees.get(address, 0))
        for _ in range(reader.number()):
            reader.number()
            reader.blob()
        for _ in range(reader.number()):
            reader.text()
            addresses = reader.number()
            reader.pos += addresses * 24 + 28
        assert reader.pos == len(data), "Reader did not consume the complete file"

        reader.pos = record_start
        allocations = defaultdict(lambda: [0, 0])
        for _ in range(record_count):
            uuid = reader.blob()
            seq, _, size, address, _, _ = struct.unpack_from(">IiiQQI", data, reader.pos)
            reader.pos += 32
            if seq < frees.get(address, 0):
                continue
            assert size >= 0
            allocations[uuid][0] += size
            allocations[uuid][1] += 1
        paths = defaultdict(lambda: [0, 0])
        for uuid, value in allocations.items():
            path = []
            for key, address in reversed(stacks.get(uuid, [])):
                library = libraries.get(key, f"[unknown library {key}]")
                name = symbols.get(library, {}).get(address) or f"0x{address:x}"
                path.append((library, name))
            if not path:
                path = [("", "[missing call stack]")]
            target = paths[tuple(path)]
            target[0] += value[0]
            target[1] += value[1]
        totals = tuple(sum(value[i] for value in allocations.values()) for i in (0, 1))
        print(f"Independent reader: {Path(stream.name).name}: {record_count:,} records, "
              f"{len(paths):,} allocation paths, {totals[0]:,} live bytes, {totals[1]:,} allocations")
        return dict(paths), totals


def expected_diff(a, b):
    self_values = {}
    inclusive = defaultdict(lambda: [0, 0])
    for path in a.keys() | b.keys():
        av, bv = a.get(path, (0, 0)), b.get(path, (0, 0))
        delta = bv[0] - av[0], bv[1] - av[1]
        if delta == (0, 0):
            continue
        self_values[path] = delta
        for depth in range(1, len(path) + 1):
            parent = inclusive[path[:depth]]
            parent[0] += delta[0]
            parent[1] += delta[1]
    return {path: tuple(value) + self_values.get(path, (0, 0)) for path, value in inclusive.items()}


def parse_report(path):
    header, nodes, stack = {}, {}, []
    in_tree = False
    with path.open(encoding="utf-8") as stream:
        for raw in stream:
            line = raw.rstrip("\n")
            if line.startswith("=== Memory Diff"):
                in_tree = True
                continue
            if not in_tree:
                if ": " in line:
                    key, value = line.split(": ", 1)
                    header[key] = value
                continue
            if not line.strip() or line.startswith("Columns:"):
                continue
            depth = (len(line) - len(line.lstrip(" "))) // 4
            label, *values = line.strip().rsplit(", ", 4)
            function, library = label.rsplit(" [", 1)
            assert library.endswith("]")
            stack[depth:] = [(library[:-1], function)]
            assert len(stack) == depth + 1
            key = tuple(stack)
            assert key not in nodes, f"Duplicate report path: {key}"
            nodes[key] = tuple(map(int, values))
    return header, nodes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path)
    parser.add_argument("comparison", type=Path)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--forward", type=Path)
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    a, at = read_capture(args.base)
    b, bt = read_capture(args.comparison)
    for name, base, comparison, x, y, xt, yt in (
        ("forward", args.base, args.comparison, a, b, at, bt),
        ("reverse", args.comparison, args.base, b, a, bt, at),
        ("identical", args.base, args.base, a, a, at, at),
    ):
        report = args.forward if name == "forward" and args.forward else args.out_dir / f"compare-{name}.txt"
        if not (name == "forward" and args.forward):
            subprocess.run([str(args.cli), "--compare", str(base), str(comparison), "--out", str(report)], check=True)
        header, actual = parse_report(report)
        expected = expected_diff(x, y)
        assert int(header["Baseline total bytes"]) == xt[0]
        assert int(header["Comparison total bytes"]) == yt[0]
        assert int(header["Baseline allocations"]) == xt[1]
        assert int(header["Comparison allocations"]) == yt[1]
        assert int(header["Size delta bytes"]) == yt[0] - xt[0]
        assert int(header["Count delta"]) == yt[1] - xt[1]
        assert actual == expected, f"{name}: diff nodes do not match the independent reference"
        print(f"PASS {name}: exact match for all {len(actual):,} nodes, inclusive/self bytes and counts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
