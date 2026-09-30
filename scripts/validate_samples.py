#!/usr/bin/env python3
"""Validate archived .loli captures with native CLI and Python agent queries."""

from __future__ import annotations

import argparse
from array import array
import hashlib
import json
import mmap
from pathlib import Path, PurePosixPath
import re
import shutil
import sqlite3
import struct
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from agentcli import core


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest() if hasattr(hashlib, "file_digest") else _digest(stream)


def _digest(stream) -> str:
    result = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        result.update(block)
    return result.hexdigest()


def audit_records(path: Path) -> dict:
    """Read allocation accounting independently of the native serializer/tree."""
    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        offset = 0

        def take(fmt: str):
            nonlocal offset
            values = struct.unpack_from(fmt, data, offset)
            offset += struct.calcsize(fmt)
            return values[0] if len(values) == 1 else values

        def skip_string():
            nonlocal offset
            length = take(">I")
            if length != 0xFFFFFFFF:
                offset += length
            require(offset <= len(data), "truncated string")

        require(take(">I") == 0xA4B3C2D1, "bad .loli magic")
        version = take(">i")
        require(version == 106, f"unsupported .loli version {version}")
        take(">i")  # max meminfo
        timelines = []
        for _ in range(take(">I")):
            points = take(">I")
            timelines.append(dict(points=points,
                                  first_seconds=struct.unpack_from(">d", data, offset)[0] if points else None,
                                  last_seconds=struct.unpack_from(">d", data, offset + (points - 1) * 16)[0] if points else None))
            offset += points * 16
        for _ in range(take(">I")):
            take(">I")
            skip_string()
        count = take(">I")
        records = array("Q")
        gross = 0
        first_ms, last_ms = None, None
        for _ in range(count):
            skip_string()
            seq, timestamp, size, address, _, _ = take(">IiiQQI")
            require(size >= 0, "negative allocation size")
            records.extend((seq, size, address))
            gross += size
            first_ms = timestamp if first_ms is None else min(first_ms, timestamp)
            last_ms = timestamp if last_ms is None else max(last_ms, timestamp)
        stacks = take(">I")
        frame_count = 0
        for _ in range(stacks):
            skip_string()
            frames = take(">I")
            frame_count += frames
            offset += frames * 12
        symbol_count = 0
        for _ in range(take(">I")):
            skip_string()
            symbols = take(">I")
            symbol_count += symbols
            for _ in range(symbols):
                take(">Q")
                skip_string()
        frees = {}
        for _ in range(take(">I")):
            address, seq = take(">QI")
            frees.setdefault(address, seq)
        live_count, live_bytes = 0, 0
        for index in range(0, len(records), 3):
            seq, size, address = records[index:index + 3]
            if seq >= frees.get(address, 0):
                live_count += 1
                live_bytes += size
        screenshot_times = []
        for _ in range(take(">I")):
            screenshot_times.append(take(">i"))
            length = take(">I")
            if length != 0xFFFFFFFF:
                offset += length
        smaps_sections = take(">I")
        for _ in range(smaps_sections):
            skip_string()
            addresses = take(">I")
            offset += addresses * 24 + 7 * 4
        require(offset <= len(data), "truncated capture payload")
        return dict(version=version, records=count, gross_bytes=gross,
                    live_records=live_count, live_bytes=live_bytes,
                    free_addresses=len(frees), first_ms=first_ms, last_ms=last_ms,
                    timelines=timelines, callstacks=stacks, frames=frame_count,
                    symbols=symbol_count, screenshot_seconds=screenshot_times,
                    smaps_sections=smaps_sections)


def command(cli: Path, args: list[str], log: Path) -> str:
    with log.open("w") as stream:
        subprocess.run([str(cli), *args], stdout=stream, stderr=subprocess.STDOUT,
                       check=True, timeout=600)
    return log.read_text()


def validate_capture(cli: Path, capture: Path, output: Path) -> dict:
    started = time.monotonic()
    name = capture.stem
    raw = audit_records(capture)
    database = output / f"{name}.db"
    command(cli, ["--dump", str(capture), "--out", str(database)], output / f"{name}.dump.log")
    with sqlite3.connect(database) as db:
        require(db.execute("PRAGMA integrity_check").fetchone()[0] == "ok", "SQLite integrity check failed")
        require(not db.execute("PRAGMA foreign_key_check").fetchall(), "SQLite foreign key check failed")
        metadata = dict(db.execute("SELECT key,value FROM metadata"))
        nodes = db.execute("SELECT COUNT(*) FROM nodes").fetchone()[0]
        roots, root_bytes, root_count = db.execute(
            "SELECT COUNT(*),SUM(size_bytes),SUM(count) FROM nodes WHERE parent_id IS NULL").fetchone()
        require(nodes > 0 and roots > 0, "empty call tree")
        require(metadata["magic"] == "loli" and metadata["schema_version"] == "1", "bad database metadata")
        require(int(metadata["node_count"]) == nodes and int(metadata["root_count"]) == roots, "tree count mismatch")
        require(int(metadata["total_allocations"]) == raw["live_records"], "raw/live allocation count mismatch")
        require(int(metadata["total_size_bytes"]) == raw["live_bytes"], "raw/live byte total mismatch")
        require(root_bytes == raw["live_bytes"] and root_count == raw["live_records"], "root accounting mismatch")
        require(not db.execute("SELECT n.id FROM nodes n JOIN nodes p ON p.id=n.parent_id WHERE n.depth != p.depth+1 LIMIT 1").fetchall(), "invalid parent depth")
        target = db.execute("SELECT id FROM nodes ORDER BY depth DESC,size_bytes DESC LIMIT 1").fetchone()[0]
        expected_path = db.execute("SELECT depth FROM nodes WHERE id=?", (target,)).fetchone()[0] + 1
    summary = core.summary(str(database))["data"]
    require(summary["summary"]["total_allocations"] == raw["live_records"], "agent summary mismatch")
    require(summary["metadata"]["node_count"] == nodes, "agent node count mismatch")
    top = core.top(str(database), n=5)["data"]["results"]
    require(top and top[0]["size_bytes"] > 0, "agent hotspot query empty")
    chain = core.call_path(str(database), target)["data"]
    require(chain["frame_count"] == expected_path and chain["frames"][-1]["node_id"] == target, "agent call path mismatch")
    text_file = output / f"{name}.txt"
    command(cli, ["--dump", str(capture), "--out", str(text_file)], output / f"{name}.text.log")
    with text_file.open() as stream:
        header = stream.read(256)
    require(f"Total allocations: {raw['live_records']}\n" in header, "text allocation count mismatch")
    comparison = command(cli, ["--compare", str(capture), str(capture), "--out", str(output / f"{name}.self.txt")], output / f"{name}.self.log")
    require(re.search(r"Changed allocation stacks: 0\s", comparison) is not None, "nonzero self-comparison changes")
    require(re.search(r"New allocation stacks: 0\s", comparison) is not None, "nonzero self-comparison new allocations")
    require(re.search(r"Removed allocation stacks: 0\s", comparison) is not None, "nonzero self-comparison removed allocations")
    return dict(capture=str(capture), sha256=digest(capture), raw=raw,
                nodes=nodes, roots=roots, database=str(database), top=top,
                call_path_frames=chain["frame_count"], seconds=round(time.monotonic() - started, 2))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--samples", type=Path, help="directory containing .loli.zip archives")
    inputs.add_argument("--capture", type=Path, action="append", help="saved .loli file; repeat for multiple captures")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "build" / "macos-validation")
    args = parser.parse_args()
    report = {"status": "running", "cli": str(args.cli.resolve()), "samples": []}
    output = args.out_dir.resolve()
    try:
        source = args.samples.resolve() if args.samples else None
        if source:
            require(output != source and source not in output.parents, "output must be outside the source sample directory")
        archives = sorted(source.glob("*.loli.zip")) if source else []
        require(bool(archives or args.capture), "no .loli.zip archives found")
        output.mkdir(parents=True, exist_ok=True)
        sample_dir = output / "samples"
        sample_dir.mkdir(exist_ok=True)
        captures = []
        for path in args.capture or []:
            capture = path.resolve()
            before = digest(capture)
            print(f"Validating {capture.name}", flush=True)
            result = validate_capture(args.cli.resolve(), capture, output)
            require(digest(capture) == before, "source capture changed")
            report["samples"].append(result)
            captures.append(capture)
            print(f"Passed: {result['raw']['live_records']:,} live records, {result['nodes']:,} tree nodes", flush=True)
        for archive in archives:
            before = digest(archive)
            with zipfile.ZipFile(archive) as zipped:
                members = [i for i in zipped.infolist() if i.filename.endswith(".loli")]
                require(len(members) == 1, f"expected one .loli member in {archive}")
                member = members[0]
                parts = PurePosixPath(member.filename).parts
                require(len(parts) == 1 and parts[0] not in (".", "..") and "\\" not in member.filename, "unsafe archive member")
                capture = sample_dir / parts[0]
                with zipped.open(member) as incoming, capture.open("wb") as outgoing:
                    shutil.copyfileobj(incoming, outgoing)
            print(f"Validating {archive.name}", flush=True)
            result = validate_capture(args.cli.resolve(), capture, output)
            require(digest(archive) == before, "source archive changed")
            result.update(archive=str(archive), archive_sha256=before)
            report["samples"].append(result)
            captures.append(capture)
            (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
            print(f"Passed: {result['raw']['live_records']:,} live records, {result['nodes']:,} tree nodes", flush=True)
        if len(captures) > 1:
            comparison = command(args.cli.resolve(), ["--compare", str(captures[0]), str(captures[1]), "--out", str(output / "cross.txt")], output / "cross.log")
            require("Comparison Results" in comparison and (output / "cross.txt").stat().st_size > 0, "cross comparison failed")
            report["cross_comparison"] = comparison
        report["status"] = "passed"
    except (OSError, RuntimeError, ValueError, struct.error, sqlite3.Error, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        report.update(status="failed", error=str(error))
        print(f"Validation failed: {error}", file=sys.stderr)
    if output.is_dir():
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
