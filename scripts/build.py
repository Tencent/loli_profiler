#!/usr/bin/env python3
"""Interactive and scriptable Qt-free LoliProfiler build driver."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile

from package_release import GCC_ABIS, LLVM_ABIS, ROOT, find_build_output


BUILD_DIR = ROOT / "build" / "cmake"
DEFAULT_NDK_VERSION = "27.0.12077973"
SDK_DOWNLOAD_PAGE = "https://developer.android.com/studio"

# Official command-line-tools archives and SHA-256 values from the Android
# Studio download page, pinned on 2026-09-28. sdkmanager installs the SDK
# Platform-Tools and side-by-side NDK after the user accepts Google's terms.
CMDLINE_TOOLS = {
    "windows": (
        "commandlinetools-win-15859902_latest.zip",
        "90ae805d20434428bffcb699c290860f19bb5f66a67e6b330067e3de801fb04a"),
    "macos-x86_64": (
        "commandlinetools-mac_x86_64-15859902_latest.zip",
        "c5a6378ab5cf7e0d5701921405115befff13e9ff7417fb588389338f8bd050f3"),
    "macos-arm64": (
        "commandlinetools-mac_arm64-15859902_latest.zip",
        "835b62a26162b229b441d1f6d4680383815a270809eb33522c0d480fa5002c4e"),
    "linux": (
        "commandlinetools-linux-15859902_latest.zip",
        "4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583"),
}


def host_platform() -> str:
    name = platform.system().lower()
    return {"windows": "windows", "darwin": "macos", "linux": "linux"}.get(name, name)


def default_sdk_root(system: str) -> Path:
    if system == "windows":
        return Path(os.environ.get("LOCALAPPDATA", str(Path.home() / "AppData" / "Local"))) / "Android" / "Sdk"
    if system == "macos":
        return Path.home() / "Library" / "Android" / "sdk"
    return Path.home() / "Android" / "Sdk"


def adb_path(sdk: Path, system: str) -> Path:
    return sdk / "platform-tools" / ("adb.exe" if system == "windows" else "adb")


def ndk_build_path(ndk: Path, system: str) -> Path:
    return ndk / ("ndk-build.cmd" if system == "windows" else "ndk-build")


def sdkmanager_path(sdk: Path, system: str) -> Path:
    return sdk / "cmdline-tools" / "latest" / "bin" / ("sdkmanager.bat" if system == "windows" else "sdkmanager")


def discover_sdk(explicit: str | None, system: str) -> Path:
    if explicit:
        return Path(explicit).expanduser().resolve()
    candidates = [os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT")]
    candidates.append(str(default_sdk_root(system)))
    adb = shutil.which("adb")
    if adb:
        candidates.append(str(Path(adb).resolve().parent.parent))
    for value in candidates:
        if value and adb_path(Path(value), system).is_file():
            return Path(value).expanduser().resolve()
    for value in candidates:
        if value and sdkmanager_path(Path(value), system).is_file():
            return Path(value).expanduser().resolve()
    return default_sdk_root(system).resolve()


def discover_ndk(explicit: str | None, sdk: Path, system: str) -> Path | None:
    if explicit:
        path = Path(explicit).expanduser().resolve()
        return path.parent if path.is_file() else path
    for value in (os.environ.get("Ndk_R20_CMD"), os.environ.get("ANDROID_NDK_HOME"),
                  os.environ.get("ANDROID_NDK_ROOT")):
        if value:
            path = Path(value).expanduser().resolve()
            root = path.parent if path.is_file() else path
            if ndk_build_path(root, system).is_file():
                return root
    versions = sdk / "ndk"
    if versions.is_dir():
        installed = [path for path in versions.iterdir()
                     if path.is_dir() and ndk_build_path(path, system).is_file()]
        if installed:
            def version_key(path: Path) -> tuple[int, ...]:
                return tuple(int(part) for part in re.findall(r"\d+", path.name))
            return max(installed, key=version_key).resolve()
    return None


def ask(question: str, *, default: bool = False) -> bool:
    suffix = "[Y/n]" if default else "[y/N]"
    while True:
        try:
            response = input(f"{question} {suffix} ").strip().lower()
        except EOFError:
            return False
        if not response:
            return default
        if response in ("y", "yes"):
            return True
        if response in ("n", "no"):
            return False
        print("Enter y or n.")


def menu() -> str | None:
    print("\nLoliProfiler build")
    print("  1. Build profiling GUI, compare GUI, CLI, Android hooks, and release zip")
    print("  2. Build profiling GUI, compare GUI, and CLI only")
    print("  3. Package existing binaries and hooks")
    print("  4. Install missing Android SDK/NDK tools")
    print("  q. Quit")
    while True:
        try:
            choice = input("Select: ").strip().lower()
        except EOFError:
            return None
        selected = {"1": "full", "2": "native", "3": "package", "4": "tools", "q": None}
        if choice in selected:
            return selected[choice]
        print("Choose 1, 2, 3, 4, or q.")


def java_environment(system: str) -> dict[str, str]:
    environment = os.environ.copy()
    roots = [os.environ.get("JAVA_HOME")]
    if system == "windows":
        roots.append(str(Path(os.environ.get("PROGRAMFILES", "C:/Program Files")) /
                         "Android" / "Android Studio" / "jbr"))
    elif system == "macos":
        roots.append("/Applications/Android Studio.app/Contents/jbr/Contents/Home")
    java_on_path = shutil.which("java")
    if java_on_path:
        roots.append(str(Path(java_on_path).resolve().parent.parent))
    for value in roots:
        if value:
            root = Path(value)
            executable = root / "bin" / ("java.exe" if system == "windows" else "java")
            if executable.is_file():
                result = subprocess.run([str(executable), "-version"],
                                        capture_output=True, text=True, check=False)
                match = re.search(r'"(\d+)(?:\.|\")', result.stderr + result.stdout)
                if match and int(match.group(1)) >= 17:
                    environment["JAVA_HOME"] = str(root)
                    environment["PATH"] = str(executable.parent) + os.pathsep + environment.get("PATH", "")
                    return environment
    raise RuntimeError("Google's current sdkmanager needs JDK 17 or newer. Install one or set JAVA_HOME before downloading SDK packages.")


def archive_key(system: str) -> str:
    if system != "macos":
        return system
    machine = platform.machine().lower()
    return "macos-arm64" if machine in ("arm64", "aarch64") else "macos-x86_64"


def download_commandline_tools(sdk: Path, system: str) -> None:
    key = archive_key(system)
    if key not in CMDLINE_TOOLS:
        raise RuntimeError(f"No pinned Android command-line-tools archive for {key}.")
    filename, expected = CMDLINE_TOOLS[key]
    url = "https://dl.google.com/android/repository/" + filename
    cache = sdk / ".downloads"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / filename
    digest = hashlib.sha256()
    if archive.is_file():
        with archive.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    if digest.hexdigest() != expected:
        print(f"Downloading {url}")
        partial = cache / (filename + ".part")
        digest = hashlib.sha256()
        request = urllib.request.Request(url, headers={"User-Agent": "LoliProfiler-build/1"})
        with urllib.request.urlopen(request, timeout=60) as response, partial.open("wb") as output:
            for block in iter(lambda: response.read(1024 * 1024), b""):
                output.write(block)
                digest.update(block)
        if digest.hexdigest() != expected:
            raise RuntimeError("Android command-line-tools SHA-256 did not match the official download page; archive was not extracted.")
        os.replace(partial, archive)

    destination = sdk / "cmdline-tools" / "latest"
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as source:
        for member in source.infolist():
            parts = PurePosixPath(member.filename).parts
            if not parts or parts[0] != "cmdline-tools":
                continue
            relative = parts[1:]
            if not relative or any(part in ("", ".", "..") for part in relative):
                continue
            target = destination.joinpath(*relative)
            try:
                target.resolve().relative_to(destination.resolve())
            except ValueError as error:
                raise RuntimeError("Unsafe path in Android tools archive") from error
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.open(member) as source_file, target.open("wb") as output:
                shutil.copyfileobj(source_file, output)
            if system != "windows":
                mode = (member.external_attr >> 16) & 0o777
                if mode:
                    target.chmod(mode)
    if not sdkmanager_path(sdk, system).is_file():
        raise RuntimeError("Downloaded command-line tools did not contain sdkmanager.")


def install_sdk_packages(sdk: Path, system: str, packages: list[str], *, interactive: bool) -> None:
    manager = sdkmanager_path(sdk, system)
    if not manager.is_file():
        raise RuntimeError("sdkmanager is missing after command-line-tools setup.")
    command = [str(manager), f"--sdk_root={sdk}", "--install", *packages]
    print("Installing Android SDK packages:", ", ".join(packages))
    print("Google's sdkmanager will request acceptance of any missing Android SDK licenses.")
    result = subprocess.run(command, cwd=ROOT, env=java_environment(system),
                            stdin=None if interactive else subprocess.DEVNULL, check=False)
    if result.returncode:
        raise RuntimeError("sdkmanager failed. Check JAVA_HOME and run sdkmanager --licenses interactively, then retry.")


def ensure_android_tools(args: argparse.Namespace, sdk: Path, ndk: Path | None,
                         system: str, *, require_ndk: bool) -> Path | None:
    missing_sdk = not adb_path(sdk, system).is_file()
    missing_ndk = ndk is None or not ndk_build_path(ndk, system).is_file()
    if args.ndk and missing_ndk:
        raise RuntimeError(f"The --ndk path has no ndk-build: {args.ndk}")
    if not missing_sdk and (not require_ndk or not missing_ndk):
        return ndk

    needs_install = missing_sdk or (require_ndk and missing_ndk)
    if needs_install and not args.download_missing:
        if args.non_interactive:
            if require_ndk and missing_ndk:
                raise RuntimeError("NDK not found. Set --ndk/ANDROID_NDK_HOME, or run interactively to approve a download.")
            print(f"SDK Platform-Tools not found at {sdk}; continuing without SDK download.")
            return ndk
        print(f"Missing: {'SDK Platform-Tools ' if missing_sdk else ''}"
              f"{'Android NDK' if require_ndk and missing_ndk else ''}")
        print(f"Official Android tools would be installed under: {sdk}")
        print(f"Android SDK download terms: {SDK_DOWNLOAD_PAGE}")
        if not ask("Have you read and agreed to the Android SDK terms, and do you want to download the missing tools?"):
            if require_ndk and missing_ndk:
                raise RuntimeError("NDK download declined. Choose a desktop build (--mode native) or provide an installed NDK.")
            return ndk

    # --download-missing is explicit consent. License acceptance still belongs
    # to sdkmanager and is never fed an automatic 'yes'.
    if args.download_missing:
        print(f"Official Android SDK download terms: {SDK_DOWNLOAD_PAGE}")
    if not sdkmanager_path(sdk, system).is_file():
        java_environment(system)  # fail before downloading unusable tooling
        download_commandline_tools(sdk, system)
    packages = []
    if missing_sdk:
        packages.append("platform-tools")
    if require_ndk and missing_ndk:
        packages.append(f"ndk;{args.ndk_version}")
    if packages:
        install_sdk_packages(sdk, system, packages, interactive=not args.non_interactive)
    found = discover_ndk(None, sdk, system)
    if require_ndk and found is None:
        raise RuntimeError("NDK install completed but ndk-build was not found under the SDK.")
    return found or ndk


def run(command: list[str], *, dry_run: bool) -> None:
    print("+", subprocess.list2cmdline(command) if os.name == "nt" else " ".join(command), flush=True)
    if not dry_run:
        subprocess.run(command, cwd=ROOT, check=True)


def stage_runtime(system: str) -> None:
    if system == "windows":
        destination = BUILD_DIR / "bin" / "release"
        destination.mkdir(parents=True, exist_ok=True)
        for name in ("LoliProfilerImGui.exe", "LoliProfilerCLI.exe", "LoliProfilerCompare.exe"):
            shutil.copy2(find_build_output(BUILD_DIR, name), destination / name)
        runtime_dirs = (destination, BUILD_DIR / "Release")
    elif system == "macos":
        runtime_dirs = (find_build_output(BUILD_DIR, "LoliProfilerCLI").parent,
                        find_build_output(BUILD_DIR, "LoliProfilerImGui.app") /
                        "Contents" / "Resources")
    else:
        runtime_dirs = (find_build_output(BUILD_DIR, "LoliProfilerCLI").parent,)
    # Keep every launch location paired with the freshly built runtime.
    for runtime_dir in runtime_dirs:
        icon = runtime_dir / "res"
        icon.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "res" / "loli_cat_icon.png", icon / "loli_cat_icon.png")
        for name in ("jdwp-shellifier.py", "logging.json"):
            shutil.copy2(ROOT / "python" / name, runtime_dir / name)
        for compiler, abis in (("llvm", LLVM_ABIS), ("gcc", GCC_ABIS)):
            for abi in abis:
                source = ROOT / "plugins" / "Android" / compiler / abi / "libloli.so"
                if source.is_file():
                    target = runtime_dir / "remote" / compiler / abi
                    target.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, target / "libloli.so")
    print("Staged local runtime:", ", ".join(str(path) for path in runtime_dirs))


def build_native(system: str, *, dry_run: bool) -> None:
    command = ["cmake", "-S", str(ROOT), "-B", str(BUILD_DIR),
               "-DBUILD_QTFREE_CORE=ON", "-DBUILD_QTFREE_CLI=ON", "-DBUILD_IMGUI_GUI=ON"]
    if system == "windows":
        command += ["-G", "Visual Studio 17 2022", "-A", "x64"]
    else:
        command += ["-DCMAKE_BUILD_TYPE=Release"]
    run(command, dry_run=dry_run)
    run(["cmake", "--build", str(BUILD_DIR), "--config", "Release",
         "--target", "LoliProfilerImGui", "LoliProfilerCLI", "LoliProfilerCompare", "--parallel"],
        dry_run=dry_run)
    if not dry_run:
        stage_runtime(system)


def build_hooks(ndk: Path, system: str, *, dry_run: bool) -> None:
    executable = ndk_build_path(ndk, system)
    plugin = ROOT / "plugins" / "Android"
    # ndk-build's default obj/ directory is shared across toolchains. Reusing
    # objects from another NDK can link against the wrong libc++/sysroot.
    ndk_identity = hashlib.sha256(str(ndk.resolve()).encode("utf-8")).hexdigest()[:12]
    llvm_obj = ROOT / "build" / "ndk-obj" / f"{system}-{ndk.name}-{ndk_identity}-llvm"
    run([str(executable), f"NDK_PROJECT_PATH={plugin}", "LLVM=1",
         f"NDK_LIBS_OUT={plugin / 'llvm'}", f"NDK_OUT={llvm_obj}"],
        dry_run=dry_run)
    gcc = os.environ.get("Ndk_R16_CMD")
    if gcc and Path(gcc).is_file():
        gcc_path = Path(gcc).resolve()
        gcc_identity = hashlib.sha256(str(gcc_path).encode("utf-8")).hexdigest()[:12]
        gcc_obj = ROOT / "build" / "ndk-obj" / f"{system}-{gcc_identity}-gcc"
        run([gcc, f"NDK_PROJECT_PATH={plugin}",
             f"NDK_LIBS_OUT={plugin / 'gcc'}", f"NDK_OUT={gcc_obj}"],
            dry_run=dry_run)


def package(system: str, *, dry_run: bool) -> None:
    run([sys.executable, str(ROOT / "scripts" / "package_release.py"),
         "--platform", system, "--build-dir", str(BUILD_DIR),
         "--out-dir", str(ROOT / "dist")], dry_run=dry_run)


def build_in_docker(mode: str, *, dry_run: bool) -> None:
    if mode == "tools":
        raise RuntimeError("The Docker image already contains an NDK; use full, native, or package mode.")
    if not dry_run and not shutil.which("docker"):
        raise RuntimeError("Docker was not found. Install Docker or run a host build.")
    run(["docker", "build", "-t", "loli_profiler", "-f",
         str(ROOT / "Dockerfile"), str(ROOT)], dry_run=dry_run)
    run(["docker", "run", "--rm", "-v", f"{ROOT}:/workspace/loli_profiler",
         "-w", "/workspace/loli_profiler", "loli_profiler", "python3",
         "scripts/build.py", "--mode", mode, "--platform", "linux",
         "--non-interactive"], dry_run=dry_run)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("full", "native", "package", "tools"))
    parser.add_argument("--platform", choices=("windows", "macos", "linux"),
                        default=host_platform())
    parser.add_argument("--sdk", help="Android SDK root; can be a new install location")
    parser.add_argument("--ndk", help="existing Android NDK root or ndk-build path")
    parser.add_argument("--ndk-version", default=DEFAULT_NDK_VERSION,
                        help="side-by-side NDK version to install with sdkmanager")
    parser.add_argument("--download-missing", action="store_true",
                        help="explicitly approve official Android tool downloads and their SDK terms")
    parser.add_argument("--non-interactive", action="store_true")
    parser.add_argument("--interactive", action="store_true",
                        help="force the menu even when stdin is redirected")
    parser.add_argument("--dry-run", action="store_true",
                        help="show build commands without downloading or building")
    parser.add_argument("--docker", action="store_true",
                        help="build a Linux release through Docker")
    args = parser.parse_args()
    if args.non_interactive and args.interactive:
        parser.error("--interactive and --non-interactive cannot be combined")
    if args.non_interactive and args.mode is None:
        parser.error("--mode is required for non-interactive runs")
    return args


def main() -> int:
    args = parse_args()
    if args.mode is None:
        if not (sys.stdin.isatty() or args.interactive):
            print("No terminal is attached. Choose --mode full, native, package, or tools.", file=sys.stderr)
            return 2
        args.mode = menu()
        if args.mode is None:
            return 0
    if args.docker:
        try:
            if args.download_missing:
                raise RuntimeError("--download-missing is not used with --docker; the image has an NDK.")
            build_in_docker(args.mode, dry_run=args.dry_run)
            return 0
        except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
            print(f"Build failed: {error}", file=sys.stderr)
            return 1
    sdk = discover_sdk(args.sdk, args.platform)
    ndk = discover_ndk(args.ndk, sdk, args.platform)
    print("SDK:", sdk, "(adb found)" if adb_path(sdk, args.platform).is_file() else "(not installed)", flush=True)
    print("NDK:", ndk if ndk else "not installed", flush=True)
    try:
        if args.mode in ("full", "tools"):
            if args.dry_run:
                print("Dry run: downloads are disabled; missing Android tools are not installed.")
                if ndk is None:
                    print("Would request consent to install NDK", args.ndk_version,
                          "under", sdk)
            else:
                ndk = ensure_android_tools(args, sdk, ndk, args.platform, require_ndk=True)
        if args.mode == "tools":
            return 0
        if args.mode in ("full", "native"):
            build_native(args.platform, dry_run=args.dry_run)
        if args.mode == "full":
            if ndk is None:
                if args.dry_run:
                    print("Would build Android hooks after an approved NDK install.")
                else:
                    raise RuntimeError("NDK is required for a capture-ready release.")
            else:
                build_hooks(ndk, args.platform, dry_run=args.dry_run)
            if not args.dry_run:
                stage_runtime(args.platform)
        if args.mode in ("full", "package"):
            package(args.platform, dry_run=args.dry_run)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Build failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
