#!/usr/bin/env python3
"""Build a deployable LoliProfiler zip from already-built binaries and hooks."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys
import zipfile


ROOT = Path(__file__).resolve().parents[1]
LLVM_ABIS = ("armeabi-v7a", "arm64-v8a", "x86", "x86_64")
GCC_ABIS = ("armeabi", "armeabi-v7a", "arm64-v8a")


def find_build_output(build_dir: Path, name: str) -> Path:
    for candidate in (build_dir / "Release" / name,
                      build_dir / name,
                      build_dir / "bin" / "release" / name):
        if candidate.exists():
            return candidate
    raise FileNotFoundError(f"built artifact not found: {name} (under {build_dir})")


def add_file(entries: dict[str, Path], source: Path, destination: str) -> None:
    if not source.is_file():
        raise FileNotFoundError(f"required release file is missing: {source}")
    entries[f"LoliProfiler/{destination}"] = source


def add_tree(entries: dict[str, Path], source: Path, destination: str) -> None:
    if not source.is_dir():
        raise FileNotFoundError(f"required release directory is missing: {source}")
    for path in sorted(source.rglob("*")):
        if path.is_file() and "__pycache__" not in path.parts and path.suffix != ".pyc":
            add_file(entries, path, f"{destination}/{path.relative_to(source).as_posix()}")


def add_runtime(entries: dict[str, Path], destination: str) -> None:
    for filename in ("jdwp-shellifier.py", "logging.json"):
        add_file(entries, ROOT / "python" / filename,
                 f"{destination}/{filename}" if destination else filename)
    add_file(entries, ROOT / "res" / "loli_cat_icon.png",
             f"{destination}/res/loli_cat_icon.png" if destination else
             "res/loli_cat_icon.png")
    for abi in LLVM_ABIS:
        add_file(entries, ROOT / "plugins" / "Android" / "llvm" / abi / "libloli.so",
                 f"{destination}/remote/llvm/{abi}/libloli.so" if destination else
                 f"remote/llvm/{abi}/libloli.so")
    for abi in GCC_ABIS:
        source = ROOT / "plugins" / "Android" / "gcc" / abi / "libloli.so"
        if source.is_file():
            add_file(entries, source,
                     f"{destination}/remote/gcc/{abi}/libloli.so" if destination else
                     f"remote/gcc/{abi}/libloli.so")


def build_entries(platform: str, build_dir: Path) -> dict[str, Path]:
    entries: dict[str, Path] = {}
    if platform == "windows":
        add_file(entries, find_build_output(build_dir, "LoliProfilerImGui.exe"),
                 "LoliProfilerImGui.exe")
        add_file(entries, find_build_output(build_dir, "LoliProfilerCLI.exe"),
                 "LoliProfilerCLI.exe")
    elif platform == "macos":
        app = find_build_output(build_dir, "LoliProfilerImGui.app")
        add_tree(entries, app, "LoliProfilerImGui.app")
        add_file(entries, find_build_output(build_dir, "LoliProfilerCLI"),
                 "LoliProfilerCLI")
        # The GUI resolves tools relative to its executable inside the bundle.
        add_runtime(entries, "LoliProfilerImGui.app/Contents/MacOS")
        icon = ROOT / "res" / "loli_cat_icon.icns"
        if icon.is_file():
            add_file(entries, icon, "LoliProfilerImGui.app/Contents/Resources/loli_cat_icon.icns")
    else:
        add_file(entries, find_build_output(build_dir, "LoliProfilerImGui"),
                 "LoliProfilerImGui")
        add_file(entries, find_build_output(build_dir, "LoliProfilerCLI"),
                 "LoliProfilerCLI")

    add_runtime(entries, "")
    add_tree(entries, ROOT / "agentcli", "agentcli")
    add_tree(entries, ROOT / "docs", "docs")
    for filename in ("README.md", "LICENSE", "pyproject.toml",
                     "analyze_heap.py", "markdown_to_html.py"):
        add_file(entries, ROOT / filename, filename)
    return entries


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("windows", "macos", "linux"), required=True)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build" / "cmake")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    out_dir = args.out_dir.resolve()
    try:
        entries = build_entries(args.platform, build_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        output = out_dir / f"LoliProfiler-{args.platform}.zip"
        temporary = output.with_suffix(".zip.tmp")
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=6, allowZip64=True) as archive:
            for destination, source in sorted(entries.items()):
                archive.write(source, destination)
        with zipfile.ZipFile(temporary) as archive:
            bad = archive.testzip()
            if bad:
                raise RuntimeError(f"zip verification failed at {bad}")
        os.replace(temporary, output)
        print(f"Packaged {output} ({output.stat().st_size:,} bytes, "
              f"{len(entries)} files)")
        return 0
    except (OSError, RuntimeError) as error:
        print(f"Packaging failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
