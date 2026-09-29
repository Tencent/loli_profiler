#!/usr/bin/env python3
"""Helpers for locating LoliProfilerCLI and converting .loli files to .db.

Used by agentcli/cli.py to auto-convert raw .loli captures to a SQLite
database on first use, with on-disk caching keyed by mtime + skip-root
parameter.

The cache filename folds in the skip-root-levels value:
    skip=0  ->  foo.db
    skip=2  ->  foo.skip2.db

A sidecar lockfile (`<dest>.lock`, opened with O_EXCL) serializes parallel
conversions of the same .loli so two `loli` invocations don't both spawn
LoliProfilerCLI on a missing cache.

Public API:
    find_loli_cli() -> str | None
    convert_loli_to_db(loli_path: str, skip_root_levels: int = 0)
        -> tuple[str, str]
"""

import errno
import os
import shutil
import subprocess
import sys
import time


def find_loli_cli() -> str | None:
    """Locate the LoliProfilerCLI executable.

    Search order:
      1. LOLI_PROFILER_CLI env var (explicit override)
      2. Sibling of the package directory (i.e. the repo root the MCP server
         and the CLI both live under)
      3. System PATH
    """
    # 1. Environment variable
    env_path = os.environ.get("LOLI_PROFILER_CLI")
    if env_path and os.path.isfile(env_path):
        return env_path

    # 2. Sibling of the package directory (repo root).
    # Both platform binaries are Qt-free and can be launched directly.
    if sys.platform == "win32":
        candidates = ("LoliProfilerCLI.exe",)
    else:
        candidates = ("LoliProfilerCLI",)

    pkg_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(pkg_dir)
    for name in candidates:
        candidate = os.path.join(repo_root, name)
        if os.path.isfile(candidate):
            return candidate

    # 3. System PATH
    for name in candidates:
        found = shutil.which(name)
        if found:
            return found

    return None


def _cache_path(loli_path: str, skip_root_levels: int) -> str:
    """Compute the .db sibling path for a given (.loli, skip) pair."""
    base, _ = os.path.splitext(loli_path)
    if skip_root_levels > 0:
        return f"{base}.skip{skip_root_levels}.db"
    return f"{base}.db"


def _is_cache_fresh(loli_path: str, db_path: str) -> bool:
    """True if db_path exists, is non-empty, and is at least as new as loli_path."""
    if not os.path.isfile(db_path):
        return False
    try:
        if os.path.getsize(db_path) == 0:
            return False
        return os.path.getmtime(db_path) >= os.path.getmtime(loli_path)
    except OSError:
        return False


def _acquire_lock(lock_path: str, timeout_sec: float = 600.0) -> "int | None":
    """Acquire an O_EXCL lockfile.  Returns the fd, or None if we waited but
    another process produced the cache while we were blocked.

    The lockfile is stale-tolerant: if it's older than 30 minutes (longer than
    any reasonable conversion), we assume the holder crashed and reclaim it.
    """
    deadline = time.monotonic() + timeout_sec
    while True:
        try:
            fd = os.open(lock_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644)
            return fd
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise

        # Lock exists.  Check if it's stale.
        try:
            age = time.time() - os.path.getmtime(lock_path)
            if age > 1800:  # 30 min stale threshold
                # Reclaim by removing — best-effort, may race but that's OK.
                try:
                    os.remove(lock_path)
                    continue
                except OSError:
                    pass
        except OSError:
            # Lock disappeared between exists check and stat — retry the open.
            continue

        if time.monotonic() >= deadline:
            raise RuntimeError(
                f"Timed out waiting for {lock_path}; another conversion may be hung. "
                f"Remove the file manually if no LoliProfilerCLI process is running."
            )
        time.sleep(0.5)


def _release_lock(fd: int, lock_path: str) -> None:
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.remove(lock_path)
    except OSError:
        pass


def convert_loli_to_db(loli_path: str, skip_root_levels: int = 0) -> tuple[str, str]:
    """Convert a .loli file to a .db SQLite database using LoliProfilerCLI --dump.

    The output .db is written alongside the original .loli file, with
    skip-root-levels folded into the filename.  Returns (db_path, info_message)
    on success.  Raises RuntimeError on failure.

    Conversion is auto-cached: if a .db sibling already exists and is newer
    than (or the same age as) the .loli, the existing .db is reused.

    Concurrent invocations are serialized by an O_EXCL lockfile.  If a peer
    finishes the conversion while we're blocked, we just reuse its result.
    """
    db_path = _cache_path(loli_path, skip_root_levels)

    # Fast path: cache is already fresh.
    if _is_cache_fresh(loli_path, db_path):
        return db_path, f"Using cached {os.path.basename(db_path)}"

    cli = find_loli_cli()
    if cli is None:
        raise RuntimeError(
            "Cannot convert .loli file: LoliProfilerCLI not found. "
            "Set LOLI_PROFILER_CLI env var or place the executable next "
            "to the loli_profiler repo root."
        )

    # Serialize parallel conversions.  Whoever wins the lock does the work;
    # everyone else waits and reuses the result.
    lock_path = db_path + ".lock"
    fd = _acquire_lock(lock_path)
    try:
        # Re-check cache freshness now that we hold the lock — a peer may
        # have finished while we were waiting.
        if _is_cache_fresh(loli_path, db_path):
            return db_path, f"Using cached {os.path.basename(db_path)}"

        cmd = [cli, "--dump", loli_path, "--out", db_path]
        if skip_root_levels > 0:
            cmd += ["--skip-root-levels", str(skip_root_levels)]

        print(f"Converting .loli -> .db: {' '.join(cmd)}", file=sys.stderr)

        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=600,  # 10-minute timeout; large profiles can take a while
        )

        if result.returncode != 0:
            stderr = (result.stderr or "").strip() or (result.stdout or "").strip()
            raise RuntimeError(
                f"LoliProfilerCLI --dump failed (exit {result.returncode}): {stderr}"
            )

        if not os.path.isfile(db_path) or os.path.getsize(db_path) == 0:
            raise RuntimeError(
                f"LoliProfilerCLI produced empty or missing output: {db_path}"
            )

        return db_path, (
            f"Auto-converted {os.path.basename(loli_path)} -> "
            f"{os.path.basename(db_path)}"
        )
    finally:
        _release_lock(fd, lock_path)


# ---------------------------------------------------------------------------
# Back-compat alias.  core.py used to call convert_loli_to_txt; downstream
# tooling that imports it directly will keep working until we migrate them
# off — the alias just forwards to the .db converter (silently dropping
# skip_root_levels=0 is the same default the old API had).
# ---------------------------------------------------------------------------

def convert_loli_to_txt(loli_path: str) -> tuple[str, str]:
    """Deprecated: prefer convert_loli_to_db.  Forwards for back-compat."""
    return convert_loli_to_db(loli_path)
