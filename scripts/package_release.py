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


def add_tree(entries: dict[str, Path], source: Path, destination: str,
             *, omit_runtime_state: bool = False) -> None:
    if not source.is_dir():
        raise FileNotFoundError(f"required release directory is missing: {source}")
    for path in sorted(source.rglob("*")):
        if omit_runtime_state:
            relative = path.relative_to(source)
            if any(part in {"cache", "loli_settings.json", "loli3.conf", "imgui.ini"}
                   for part in relative.parts) or path.suffix in {".log", ".loli", ".db"}:
                continue
        if path.is_file() and "__pycache__" not in path.parts and path.suffix != ".pyc":
            add_file(entries, path, f"{destination}/{path.relative_to(source).as_posix()}")


def add_runtime(entries: dict[str, Path], destination: str, *, include_hooks: bool = True) -> None:
    for filename in ("jdwp-shellifier.py", "logging.json"):
        add_file(entries, ROOT / "python" / filename,
                 f"{destination}/{filename}" if destination else filename)
    add_file(entries, ROOT / "res" / "loli_cat_icon.png",
             f"{destination}/res/loli_cat_icon.png" if destination else
             "res/loli_cat_icon.png")
    if not include_hooks:
        return
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


def build_entries(platform: str, build_dir: Path, *, native_only: bool = False) -> dict[str, Path]:
    entries: dict[str, Path] = {}
    if platform == "windows":
        add_file(entries, find_build_output(build_dir, "LoliProfilerImGui.exe"),
                 "LoliProfilerImGui.exe")
        add_file(entries, find_build_output(build_dir, "LoliProfilerCLI.exe"),
                 "LoliProfilerCLI.exe")
        add_file(entries, find_build_output(build_dir, "LoliProfilerCompare.exe"),
                 "LoliProfilerCompare.exe")
    elif platform == "macos":
        app = find_build_output(build_dir, "LoliProfilerImGui.app")
        add_tree(entries, app, "LoliProfilerImGui.app", omit_runtime_state=True)
        if native_only:
            # Staged hooks from prior builds must not sneak into an offline ZIP.
            entries = {name: path for name, path in entries.items()
                       if "/Contents/MacOS/remote/" not in name}
        add_file(entries, find_build_output(build_dir, "LoliProfilerCLI"),
                 "LoliProfilerCLI")
        add_file(entries, find_build_output(build_dir, "LoliProfilerCompare"),
                 "LoliProfilerCompare")
        # The GUI resolves tools relative to its executable inside the bundle.
        add_runtime(entries, "LoliProfilerImGui.app/Contents/MacOS",
                    include_hooks=not native_only)
        icon = ROOT / "res" / "loli_cat_icon.icns"
        if icon.is_file():
            add_file(entries, icon, "LoliProfilerImGui.app/Contents/Resources/loli_cat_icon.icns")
    else:
        add_file(entries, find_build_output(build_dir, "LoliProfilerImGui"),
                 "LoliProfilerImGui")
        add_file(entries, find_build_output(build_dir, "LoliProfilerCLI"),
                 "LoliProfilerCLI")
        add_file(entries, find_build_output(build_dir, "LoliProfilerCompare"),
                 "LoliProfilerCompare")

    add_runtime(entries, "", include_hooks=not native_only)
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
    parser.add_argument("--native-only", action="store_true",
                        help="package offline GUI/CLI without Android hooks; archive is named *-native.zip")
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    out_dir = args.out_dir.resolve()
    try:
        entries = build_entries(args.platform, build_dir, native_only=args.native_only)
        out_dir.mkdir(parents=True, exist_ok=True)
        suffix = "-native" if args.native_only else ""
        output = out_dir / f"LoliProfiler-{args.platform}{suffix}.zip"
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
