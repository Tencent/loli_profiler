#!/usr/bin/env python3
"""
MCP server for interactive heap data exploration.

Loads a LoliProfilerCLI output file (diff from --compare, or snapshot from
--dump) and exposes query tools via the Model Context Protocol (stdio
transport), letting Claude explore the call tree interactively instead of
ingesting it all at once.

Usage:
    python heap_explorer_server.py                    # Start empty, use load_file tool
    python heap_explorer_server.py --file diff.txt    # Pre-load a file at startup
    python heap_explorer_server.py --file snapshot.txt
"""

import argparse
import subprocess
import sys
import os
import tempfile

from mcp.server.fastmcp import FastMCP

# Add parent directory so we can import tree_model (after third-party imports
# to avoid shadowing the 'mcp' package with a local directory).
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from mcp_server.tree_model import CallTreeDatabase

# ---------------------------------------------------------------------------
# Global state — populated at startup
# ---------------------------------------------------------------------------
db: CallTreeDatabase = CallTreeDatabase()

mcp = FastMCP(
    "loli-heap",
    instructions=(
        "LoliProfiler heap explorer (supports both diff and snapshot files). "
        "If no file is loaded yet, call load_file(path) first. Then use "
        "get_summary to see overview stats, get_top_allocations to find "
        "hotspots, then drill down with get_children / get_call_path / "
        "get_subtree / search_function."
    ),
)


# ---------------------------------------------------------------------------
# MCP Tools
# ---------------------------------------------------------------------------

def _find_loli_cli() -> str | None:
    """Locate LoliProfilerCLI executable.

    Search order:
      1. LOLI_PROFILER_CLI env var (explicit override)
      2. Same directory as this MCP server's parent (sibling of mcp_server/)
      3. System PATH
    """
    # 1. Environment variable
    env_path = os.environ.get("LOLI_PROFILER_CLI")
    if env_path and os.path.isfile(env_path):
        return env_path

    # 2. Sibling of mcp_server/ directory
    server_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for name in ("LoliProfilerCLI.exe", "LoliProfilerCLI"):
        candidate = os.path.join(server_dir, name)
        if os.path.isfile(candidate):
            return candidate

    # 3. System PATH
    import shutil
    for name in ("LoliProfilerCLI.exe", "LoliProfilerCLI"):
        found = shutil.which(name)
        if found:
            return found

    return None


def _convert_loli_to_txt(loli_path: str) -> tuple[str, str]:
    """Convert a .loli file to .txt using LoliProfilerCLI --dump.

    Returns (txt_path, info_message) on success.
    Raises RuntimeError on failure.
    """
    cli = _find_loli_cli()
    if cli is None:
        raise RuntimeError(
            "Cannot convert .loli file: LoliProfilerCLI not found. "
            "Set LOLI_PROFILER_CLI env var or place the executable next "
            "to the mcp_server/ directory."
        )

    # Output .txt alongside the original .loli file
    txt_path = os.path.splitext(loli_path)[0] + ".txt"

    cmd = [cli, "--dump", loli_path, "--out", txt_path]
    print(f"Converting .loli → .txt: {' '.join(cmd)}", file=sys.stderr)

    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=300,  # 5 minute timeout for large files
    )

    if result.returncode != 0:
        stderr = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(
            f"LoliProfilerCLI --dump failed (exit {result.returncode}): {stderr}"
        )

    if not os.path.isfile(txt_path) or os.path.getsize(txt_path) == 0:
        raise RuntimeError(
            f"LoliProfilerCLI produced empty or missing output: {txt_path}"
        )

    return txt_path, f"Auto-converted {os.path.basename(loli_path)} → {os.path.basename(txt_path)}"


@mcp.tool()
def load_file(file_path: str) -> str:
    """Load a heap data file (diff or snapshot) into the explorer.

    Replaces any previously loaded data. Accepts absolute paths or paths
    relative to the working directory. Call get_summary() after loading.

    Supports both .txt files (from LoliProfilerCLI --compare or --dump)
    and raw .loli files (auto-converted via LoliProfilerCLI --dump).

    Args:
        file_path: Path to .txt or .loli heap data file.
    """
    resolved = os.path.abspath(file_path)
    if not os.path.isfile(resolved):
        return f"Error: file not found: {resolved}"

    convert_msg = ""

    # Auto-convert .loli files to .txt
    if resolved.lower().endswith(".loli"):
        try:
            resolved, convert_msg = _convert_loli_to_txt(resolved)
            convert_msg += "\n"
        except RuntimeError as e:
            return f"Error: {e}"

    db.reset()
    try:
        db.load_from_file(resolved)
    except Exception as e:
        db.reset()  # Ensure clean state after failure
        return f"Error loading file: {e}"
    mode = db.summary.mode or "unknown"
    return (
        f"{convert_msg}"
        f"Loaded {db.node_count:,} nodes ({len(db.roots)} roots) "
        f"from {os.path.basename(resolved)} [mode: {mode}]"
    )


@mcp.tool()
def get_summary() -> str:
    """Get overview statistics of the loaded heap data (diff or snapshot).

    Returns header stats (allocation counts, sizes, and delta for diffs),
    tree metadata (total nodes, root count, unique functions), and the
    top 5 root nodes by size. Call this first to understand the dataset.
    """
    return db.get_summary()


@mcp.tool()
def get_top_allocations(n: int = 20, min_size_mb: float = 0.0) -> str:
    """Find the N largest allocation nodes across the entire tree.

    Searches all depths (not just roots) and returns nodes sorted by
    absolute size descending. Works for both diff files (largest growth)
    and snapshot files (largest allocations). Each result includes a
    node_id for follow-up queries.

    Args:
        n: Maximum number of results to return (default 20).
        min_size_mb: Minimum absolute size in MB to include (default 0).
    """
    return db.get_top_allocations(n=n, min_size_mb=min_size_mb)


@mcp.tool()
def get_children(node_id: int) -> str:
    """List direct children of a node, sorted by absolute size descending.

    Use this to drill down into a specific branch of the call tree.

    Args:
        node_id: The integer ID of the parent node.
    """
    return db.get_children(node_id)


@mcp.tool()
def get_call_path(node_id: int) -> str:
    """Trace the full call path from root to a specific node.

    Returns every frame in the chain with sizes and counts, useful for
    understanding the calling context that leads to an allocation.

    Args:
        node_id: The integer ID of the target node.
    """
    return db.get_call_path(node_id)


@mcp.tool()
def search_function(pattern: str, max_results: int = 30) -> str:
    """Regex search across all function names in the tree.

    Returns matching nodes sorted by absolute size descending, each with
    a node_id for follow-up. Use this to find specific modules, classes,
    or allocation patterns.

    Args:
        pattern: Regular expression to match against function names.
        max_results: Maximum results to return (default 30).
    """
    return db.search_function(pattern=pattern, max_results=max_results)


@mcp.tool()
def get_subtree(node_id: int, max_depth: int = 4) -> str:
    """Show the indented tree structure below a node.

    Renders the subtree as indented text, truncating beyond max_depth
    to keep output manageable. Children at each level are sorted by
    absolute size descending.

    Args:
        node_id: The integer ID of the root of the subtree.
        max_depth: Maximum depth to render (default 4).
    """
    return db.get_subtree(node_id=node_id, max_depth=max_depth)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="MCP server for LoliProfiler heap data exploration (diff or snapshot)")
    parser.add_argument('--file', required=False, default=None, help="Path to .txt or .loli heap data file (optional; use load_file tool to load later)")
    args = parser.parse_args()

    if args.file is not None:
        resolved = os.path.abspath(args.file)
        if not os.path.isfile(resolved):
            print(f"Error: file not found: {resolved}", file=sys.stderr)
            sys.exit(1)

        # Auto-convert .loli → .txt if needed
        if resolved.lower().endswith(".loli"):
            try:
                resolved, msg = _convert_loli_to_txt(resolved)
                print(msg, file=sys.stderr)
            except RuntimeError as e:
                print(f"Error: {e}", file=sys.stderr)
                sys.exit(1)

        # Load the heap data (auto-detects diff vs snapshot)
        db.load_from_file(resolved)
        mode = db.summary.mode or "unknown"
        print(
            f"Loaded {db.node_count} nodes ({len(db.roots)} roots) from {args.file} [mode: {mode}]",
            file=sys.stderr,
        )
    else:
        print("Server ready (no file loaded). Use load_file tool to load data.", file=sys.stderr)

    # Run MCP server on stdio
    mcp.run(transport="stdio")


if __name__ == '__main__':
    main()
