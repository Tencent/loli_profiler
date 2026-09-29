"""agentcli.core - pure functions over a LoliProfiler heap data file.

Each function takes an explicit file_path plus keyword args and returns a
plain dict of the form:

    {"text": "<formatted text suitable for direct display>",
     "data": <structured payload for --json mode>}

`text` is the human-readable rendering produced by CallTreeDatabase
methods in agentcli/tree_model.py. `data` is a parallel structured
representation (lists of dicts) for machine consumption.

Backed by a SQLite database (.db) produced by `LoliProfilerCLI --dump
foo.loli --out foo.db`.  When the user passes a .loli, we transparently
convert + cache (mtime-keyed) before opening.
"""
from __future__ import annotations

import os
from typing import Optional

from agentcli.tree_model import CallTreeDatabase, TreeNode
from agentcli.loli_convert import convert_loli_to_db


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _resolve(file_path: str, skip_root_levels: int = 0) -> tuple[str, str]:
    """Resolve any input path to a .db SQLite file.

    Accepts:
      - .loli  -> auto-converted via LoliProfilerCLI --dump (cached on disk
                  as a sibling .db, with --skip-root-levels folded into the
                  cache filename)
      - .db    -> returned as-is (we trust it; tree_model validates the magic)

    Anything else (including the legacy .txt format) is rejected with a
    helpful message.

    Returns (resolved_db_path, info_message).  info_message is empty when
    no conversion was needed.
    """
    resolved = os.path.abspath(file_path)
    if not os.path.isfile(resolved):
        raise FileNotFoundError(f"file not found: {resolved}")

    lower = resolved.lower()
    if lower.endswith(".loli"):
        resolved, msg = convert_loli_to_db(resolved, skip_root_levels=skip_root_levels)
        return resolved, msg
    if lower.endswith(".db"):
        return resolved, ""
    if lower.endswith(".txt"):
        raise RuntimeError(
            f"{resolved}: the loli CLI now reads SQLite (.db) snapshots, "
            "not .txt.  Pass the original .loli (auto-converted) or run "
            "LoliProfilerCLI --dump <file>.loli --out <file>.db first."
        )
    raise RuntimeError(
        f"{resolved}: unrecognized extension; expected .loli or .db"
    )


def _load(file_path: str, skip_root_levels: int = 0) -> tuple[CallTreeDatabase, str, str]:
    """Resolve + load the file. Returns (db, resolved_path, info_message)."""
    resolved, info = _resolve(file_path, skip_root_levels=skip_root_levels)
    db = CallTreeDatabase()
    db.load_from_file(resolved)
    return db, resolved, info


def _node_dict(node: TreeNode) -> dict:
    """Compact JSON-friendly view of a single node."""
    return {
        "node_id": node.node_id,
        "function_name": node.function_name,
        "size_bytes": node.size_bytes,
        "size_display": node.size_display,
        "count": node.count,
        "depth": node.level,
    }


# ---------------------------------------------------------------------------
# Tools - one per MCP tool, same names where reasonable
# ---------------------------------------------------------------------------

def load_file(file_path: str) -> dict:
    """Validate a heap data file: load it, report node/root counts.

    Stateless - the CLI does NOT cache the load globally (that's an
    MCP-server concept). Reports the same one-line message the MCP tool
    reports on success.
    """
    try:
        db, resolved, info = _load(file_path)
    except FileNotFoundError as e:
        return {"text": f"Error: {e}", "data": {"error": str(e)}}
    except Exception as e:
        return {"text": f"Error loading file: {e}",
                "data": {"error": f"{type(e).__name__}: {e}"}}

    mode = db.summary.mode or "unknown"
    prefix = f"{info}\n" if info else ""
    text = (
        f"{prefix}Loaded {db.node_count:,} nodes ({len(db.roots)} roots) "
        f"from {os.path.basename(resolved)} [mode: {mode}]"
    )
    return {
        "text": text,
        "data": {
            "file_path": resolved,
            "mode": mode,
            "node_count": db.node_count,
            "root_count": len(db.roots),
            "info": info,
        },
    }


def summary(file_path: str) -> dict:
    """Header stats + tree metadata + top 5 roots."""
    db, resolved, _ = _load(file_path)
    text = db.get_summary()
    s = db.summary
    # Roots are already returned sorted by size DESC; just take the top 5.
    sorted_roots = db.roots[:5]
    data = {
        "file_path": resolved,
        "mode": s.mode,
        "summary": {
            "mode": s.mode,
            "total_allocations": s.total_allocations,
            "total_size": s.total_size,
            "comparison_allocations": s.comparison_allocations,
            "comparison_total_size": s.comparison_total_size,
            "size_delta": s.size_delta,
            "changed_allocations": s.changed_allocations,
            "new_allocations": s.new_allocations,
        },
        "metadata": {
            "node_count": db.node_count,
            "root_count": db._root_count,
            "unique_function_names": db._unique_names,
        },
        "top_roots": [_node_dict(n) for n in sorted_roots],
    }
    return {"text": text, "data": data}


def top(file_path: str, n: int = 20, min_size_mb: float = 0.0) -> dict:
    """Top-N allocations across all depths, by size."""
    db, resolved, _ = _load(file_path)
    text = db.get_top_allocations(n=n, min_size_mb=min_size_mb)

    min_bytes = int(min_size_mb * 1024 * 1024)
    nodes = db.top_nodes(n=n, min_size_bytes=min_bytes)
    data = {
        "file_path": resolved,
        "n": n,
        "min_size_mb": min_size_mb,
        "count": len(nodes),
        "results": [_node_dict(nd) for nd in nodes],
    }
    return {"text": text, "data": data}


def children(file_path: str, node_id: int) -> dict:
    """Direct children of a node, sorted by size descending."""
    db, resolved, _ = _load(file_path)
    text = db.get_children(node_id)
    parent = db.node_by_id(node_id)
    if parent is None:
        return {"text": text,
                "data": {"file_path": resolved, "error": f"node_id {node_id} not found"}}
    kids = db.children_of(node_id)
    data = {
        "file_path": resolved,
        "parent": _node_dict(parent),
        "child_count": len(kids),
        "children": [_node_dict(c) for c in kids],
    }
    return {"text": text, "data": data}


def call_path(file_path: str, node_id: int) -> dict:
    """Root-to-node call path."""
    db, resolved, _ = _load(file_path)
    text = db.get_call_path(node_id)
    if db.node_by_id(node_id) is None:
        return {"text": text,
                "data": {"file_path": resolved, "error": f"node_id {node_id} not found"}}
    chain = db.parent_chain(node_id)
    data = {
        "file_path": resolved,
        "target_node_id": node_id,
        "frame_count": len(chain),
        "frames": [_node_dict(n) for n in chain],
    }
    return {"text": text, "data": data}


def search(file_path: str, pattern: str, max_results: int = 30) -> dict:
    """Regex search across all function names."""
    db, resolved, _ = _load(file_path)
    text = db.search_function(pattern=pattern, max_results=max_results)

    try:
        matches, total = db.search_nodes(pattern, max_results=max_results)
    except ValueError as e:
        return {"text": text,
                "data": {"file_path": resolved, "error": str(e)}}

    data = {
        "file_path": resolved,
        "pattern": pattern,
        "max_results": max_results,
        "total_matches": total,
        "returned": len(matches),
        "results": [_node_dict(nd) for nd in matches],
    }
    return {"text": text, "data": data}


def subtree(file_path: str, node_id: int, max_depth: int = 4) -> dict:
    """Indented subtree view below a node."""
    db, resolved, _ = _load(file_path)
    text = db.get_subtree(node_id=node_id, max_depth=max_depth)
    root = db.node_by_id(node_id)
    if root is None:
        return {"text": text,
                "data": {"file_path": resolved, "error": f"node_id {node_id} not found"}}

    def _walk(n: TreeNode, depth: int) -> dict:
        node = _node_dict(n)
        kids = db.children_of(n.node_id)
        if depth >= max_depth and kids:
            node["truncated_children"] = len(kids)
            node["children"] = []
            return node
        node["children"] = [_walk(c, depth + 1) for c in kids]
        return node

    data = {
        "file_path": resolved,
        "max_depth": max_depth,
        "root": _walk(root, 0),
    }
    return {"text": text, "data": data}


# ---------------------------------------------------------------------------
# describe — agent introspection (always JSON via the CLI layer)
# ---------------------------------------------------------------------------

COMMAND_SCHEMAS: list[dict] = [
    {
        "name": "load-file",
        "description": "Validate a heap data file (.loli auto-converted to .db) and report node/root counts.",
        "args": [
            {"name": "file_path", "type": "path", "required": True,
             "description": ".loli or .db heap data file"},
        ],
    },
    {
        "name": "summary",
        "description": "Header stats, tree metadata, and the top 5 roots by abs(size).",
        "args": [{"name": "file_path", "type": "path", "required": True}],
    },
    {
        "name": "top",
        "description": "Top-N largest allocation nodes across all depths.",
        "args": [
            {"name": "file_path", "type": "path", "required": True},
            {"name": "-n", "type": "int", "default": 20,
             "description": "Maximum results to return"},
            {"name": "--min-size-mb", "type": "float", "default": 0.0,
             "description": "Minimum absolute size in MB"},
        ],
    },
    {
        "name": "children",
        "description": "Direct children of a node, sorted by abs(size) descending.",
        "args": [
            {"name": "file_path", "type": "path", "required": True},
            {"name": "node_id", "type": "int", "required": True},
        ],
    },
    {
        "name": "call-path",
        "description": "Trace the full call path from root to the target node.",
        "args": [
            {"name": "file_path", "type": "path", "required": True},
            {"name": "node_id", "type": "int", "required": True},
        ],
    },
    {
        "name": "search",
        "description": "Regex search across all function names; results sorted by abs(size).",
        "args": [
            {"name": "file_path", "type": "path", "required": True},
            {"name": "pattern", "type": "string", "required": True,
             "description": "Regular expression (case-insensitive)"},
            {"name": "--max", "type": "int", "default": 30,
             "description": "Maximum results to return"},
        ],
    },
    {
        "name": "subtree",
        "description": "Indented subtree view below a node, truncated at max-depth.",
        "args": [
            {"name": "file_path", "type": "path", "required": True},
            {"name": "node_id", "type": "int", "required": True},
            {"name": "--max-depth", "type": "int", "default": 4},
        ],
    },
    {
        "name": "describe",
        "description": "Print the full command schema as JSON. Pass a command name to scope to one.",
        "args": [
            {"name": "command", "type": "string", "required": False,
             "description": "Optional: scope output to a single subcommand"},
        ],
    },
]


def describe(command: Optional[str] = None) -> dict:
    """Return JSON schema describing every subcommand (or one of them)."""
    if command:
        for c in COMMAND_SCHEMAS:
            if c["name"] == command:
                return c
        return {"error": f"Unknown command: {command}",
                "available": [c["name"] for c in COMMAND_SCHEMAS]}
    return {
        "tool": "loli",
        "version": "0.1.0",
        "description": "CLI for LoliProfiler heap data exploration "
                       "(snapshot from --dump, backed by a SQLite cache).",
        "commands": COMMAND_SCHEMAS,
        "input_formats": [".loli (auto-converts to .db)", ".db (read directly)"],
        "notes": [
            "Every command takes the heap file path as the first positional argument.",
            "Default output is plain text matching the MCP server's tool output.",
            "Pass --json to any subcommand for a machine-readable structured response.",
            "On first use of a .loli, a sibling .db is produced and cached "
            "(by mtime).  Subsequent invocations open the .db directly in ~1ms.",
        ],
    }
