"""End-to-end comparison tests using independently generated .loli fixtures."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


def fixture(path, current):
    def integer(value):
        return struct.pack(">I", value)

    def text(value):
        data = value.encode("utf-16-be")
        return integer(len(data)) + data

    uuids = ["{11111111-1111-1111-1111-111111111111}", "{22222222-2222-2222-2222-222222222222}"]
    records = [(0, 1, 10, 80), (1, 2, 75, 81)] if current else [(0, 1, 100, 7), (0, 3, 20, 7), (1, 4, 50, 8)]
    data = integer(0xA4B3C2D1) + integer(106) + integer(128) + integer(0)
    data += integer(1) + integer(11) + text("game.so")
    data += integer(len(records))
    for uuid, seq, size, address in records:
        data += text(uuids[uuid]) + struct.pack(">IiiQQI", seq, 0, size, address, 0, 11)
    data += integer(2)
    for uuid, frames in zip(uuids, ((1,), (2, 1))):
        data += text(uuid) + integer(len(frames))
        for address in frames:
            data += struct.pack(">IQ", 11, address)
    data += integer(1) + text("game.so") + integer(2)
    data += struct.pack(">Q", 1) + text("root") + struct.pack(">Q", 2) + text("child")
    data += integer(0) if current else integer(1) + struct.pack(">QI", 7, 2)
    data += integer(0) + integer(0)  # screenshots, smaps
    path.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--gui", type=Path)
    args = parser.parse_args()
    cli = args.cli.resolve()
    with tempfile.TemporaryDirectory(prefix="loli compare ") as directory:
        root = Path(directory)
        a, b = root / "\u57fa\u51c6 with spaces.loli", root / "comparison.loli"
        fixture(a, False)
        fixture(b, True)
        report = root / "\u5bf9\u6bd4 report.txt"
        base = [str(cli), "--compare", str(a), str(b)]
        result = subprocess.run(base + ["--out", str(report)], capture_output=True)
        assert result.returncode == 0, result.stderr
        content = report.read_text(encoding="utf-8")
        assert "Baseline total bytes: 70" in content and "Comparison total bytes: 85" in content
        assert "root [game.so], +15, +0, -10, +0" in content
        assert "child [game.so], +25, +0, +25, +0" in content
        for extra in (
            ["--skip-root-levels"], ["--skip-root-levels="], ["--skip-root-levels", "-1"],
            ["--skip-root-levels", "2junk"], ["--skip-root-levels", "9999999999999"],
            ["--skip-root-levels", "--out", str(report)], ["--bogus"],
        ):
            result = subprocess.run(base + ["--out", str(report)] + extra, capture_output=True)
            assert result.returncode != 0, f"Invalid arguments accepted: {extra}"
            assert report.read_text(encoding="utf-8") == content
        unsupported = root / "diff.LOLI"
        assert subprocess.run(base + ["--out", str(unsupported)], capture_output=True).returncode != 0
        assert not unsupported.exists()
        before = a.read_bytes()
        assert subprocess.run(base + ["--out", str(a)], capture_output=True).returncode != 0
        assert a.read_bytes() == before
        # Skipping beyond complete stacks must keep the allocation totals.
        assert subprocess.run(base + ["--out", str(report), "--skip-root-levels=99"], capture_output=True).returncode == 0
        skipped = report.read_text(encoding="utf-8")
        assert "Baseline total bytes: 70" in skipped and "[stack omitted by root skipping]" in skipped
        if args.gui:
            assert subprocess.run([str(args.gui.resolve()), "--smoke-empty"], timeout=30).returncode == 0
            assert subprocess.run([str(args.gui.resolve()), "--smoke-empty", str(a)],
                                  capture_output=True, timeout=30).returncode != 0
            gui_report = root / "gui.txt"
            assert subprocess.run([str(args.gui.resolve()), "--base", str(a), "--compare", str(b),
                                   "--smoke-test", "--out", str(gui_report)], timeout=30).returncode == 0
            assert gui_report.read_text(encoding="utf-8") == content
            assert subprocess.run([str(args.gui.resolve()), str(a), str(b), "--smoke-test", "--smoke-stacked-layout"],
                                  timeout=30).returncode == 0
            # Identical files exercise empty diff rendering too.
            assert subprocess.run([str(args.gui.resolve()), str(a), str(a), "--smoke-test"], timeout=30).returncode == 0
            for invalid in (
                ["--base", str(a), "--compare", str(root / "missing.loli")],
                ["--base", str(a), "--compare", str(b), "--skip-root-levels", "-1"],
                ["--base", str(a), "--compare", str(b), "--bogus"],
            ):
                assert subprocess.run([str(args.gui.resolve()), "--smoke-test"] + invalid,
                                      capture_output=True, timeout=30).returncode != 0
        print("PASS CLI: Unicode/spaced paths, live frees, internal self deltas, strict options, input protection, root skipping")
        if args.gui:
            print("PASS GUI: empty preview rejects captures, docked/stacked layouts, independent searches, invalid arguments, CLI parity")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
