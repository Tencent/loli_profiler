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
import sys
import os

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

@mcp.tool()
def load_file(file_path: str) -> str:
    """Load a heap data file (diff or snapshot) into the explorer.

    Replaces any previously loaded data. Accepts absolute paths or paths
    relative to the working directory. Call get_summary() after loading.

    Args:
        file_path: Path to .txt file from LoliProfilerCLI --compare or --dump.
    """
    resolved = os.path.abspath(file_path)
    if not os.path.isfile(resolved):
        return f"Error: file not found: {resolved}"
    db.reset()
    try:
        db.load_from_file(resolved)
    except Exception as e:
        db.reset()  # Ensure clean state after failure
        return f"Error loading file: {e}"
    mode = db.summary.mode or "unknown"
    return (
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
    parser.add_argument('--file', required=False, default=None, help="Path to diff.txt or snapshot.txt file (optional; use load_file tool to load later)")
    args = parser.parse_args()

    if args.file is not None:
        if not os.path.isfile(args.file):
            print(f"Error: file not found: {args.file}", file=sys.stderr)
            sys.exit(1)

        # Load the heap data (auto-detects diff vs snapshot)
        db.load_from_file(args.file)
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
