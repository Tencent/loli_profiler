#!/usr/bin/env python3
"""
SQLite-backed data model and query engine for LoliProfiler heap snapshots.

The .loli file is converted once (by `LoliProfilerCLI --dump foo.loli --out
foo.db`) into an indexed SQLite database; this module opens that database
read-only and answers every CLI query with a single SELECT or recursive
CTE. No more reparsing 1.5M lines per invocation.

Public surface mirrors what `core.py` and `cli.py` consumed before:

    db = CallTreeDatabase()
    db.load_from_file(path)
    db.summary           # FileSummary dataclass
    db.roots             # list[TreeNode] (depth-0 nodes only)
    db.node_count        # int
    db.get_summary()
    db.get_top_allocations(n=20, min_size_mb=0.0)
    db.get_children(node_id)
    db.get_call_path(node_id)
    db.search_function(pattern, max_results=30)
    db.get_subtree(node_id, max_depth=4)

Plus structured-data helpers used by `core.py` for `--json` mode:

    db.node_by_id(node_id) -> TreeNode | None
    db.children_of(node_id) -> list[TreeNode]
    db.parent_chain(node_id) -> list[TreeNode]   # root->...->node
    db.search_nodes(pattern) -> list[TreeNode]
    db.iter_subtree(root_id, max_depth) -> Iterator[(TreeNode, depth, truncated)]
"""

from __future__ import annotations

import os
import re
import sqlite3
from dataclasses import dataclass, field
from typing import List, Optional, Iterator, Tuple


# ---------------------------------------------------------------------------
# Data classes (kept identical in shape to the previous text-parser output so
# core.py / cli.py don't need to know we swapped backends).
# ---------------------------------------------------------------------------

@dataclass
class TreeNode:
    """A single node in the call stack tree (read-only view of one DB row)."""
    node_id: int
    function_name: str
    size_bytes: int           # absolute (snapshot) or signed (diff, future)
    size_display: str
    count: int
    level: int                # depth from root
    parent: Optional['TreeNode'] = field(default=None, repr=False)
    children: List['TreeNode'] = field(default_factory=list, repr=False)

    def __repr__(self) -> str:
        return (f"TreeNode(id={self.node_id}, {self.function_name}, "
                f"{self.size_display}, count={self.count})")


@dataclass
class FileSummary:
    """Header statistics from the database's metadata table."""
    mode: str = ""  # "snapshot" (compare/diff currently routed via .txt)
    total_allocations: int = 0
    total_size: str = ""

    # Diff-only fields (kept for forward compat; always 0/"" in snapshot mode).
    comparison_allocations: int = 0
    comparison_total_size: str = ""
    size_delta: str = ""
    changed_allocations: int = 0
    new_allocations: int = 0


# ---------------------------------------------------------------------------
# Size formatting (kept identical to the previous tree_model so CLI text
# output is byte-for-byte the same shape).
# ---------------------------------------------------------------------------

def format_bytes(n: int, signed: bool = False) -> str:
    """Format a byte count to the same shape as LoliProfilerCLI's sizeToString.

    Snapshot mode (signed=False) is the default — never prepend +/-.
    """
    if n == 0:
        return "0 Bytes"
    if signed:
        sign = "+" if n > 0 else "-"
    else:
        sign = ""
    a = abs(n)
    if a >= 1024 ** 3:
        return f"{sign}{a / 1024 ** 3:.2f} GB"
    if a >= 1024 ** 2:
        return f"{sign}{a / 1024 ** 2:.2f} MB"
    if a >= 1024:
        return f"{sign}{a / 1024:.2f} KB"
    return f"{sign}{a} Bytes"


# Back-compat alias for any caller that still imports the old name.
def parse_size_to_bytes(size_str: str) -> int:
    """Inverse of format_bytes; only used by legacy callers."""
    m = re.search(r'([\+\-]?[\d.]+)\s*(GB|MB|KB|Bytes?)', size_str, re.IGNORECASE)
    if not m:
        return 0
    value = float(m.group(1))
    unit = m.group(2).upper()
    mult = {'BYTES': 1, 'BYTE': 1, 'KB': 1024, 'MB': 1024 ** 2, 'GB': 1024 ** 3}
    return int(value * mult.get(unit, 1))


# ---------------------------------------------------------------------------
# CallTreeDatabase — SQLite-backed query engine
# ---------------------------------------------------------------------------

# Required column ordering used by _row_to_node.  Keep in sync with the
# CREATE TABLE in src/profilecomparator.cpp::WriteDumpToSqlite.
_NODE_COLUMNS = "id, function_name, size_bytes, count, depth"


class CallTreeDatabase:
    """Indexed read-only view of a LoliProfiler snapshot stored in SQLite."""

    SCHEMA_VERSION = "1"

    def __init__(self) -> None:
        self.summary: FileSummary = FileSummary()
        self._conn: Optional[sqlite3.Connection] = None
        self._path: str = ""
        self._node_count: int = 0
        self._root_count: int = 0
        self._unique_names: int = 0

    # ------------------------------------------------------------------
    # Properties used by core.py
    # ------------------------------------------------------------------

    @property
    def node_count(self) -> int:
        return self._node_count

    @property
    def roots(self) -> List[TreeNode]:
        """All root nodes (depth=0), sorted by abs(size) descending."""
        rows = self._conn.execute(
            f"SELECT {_NODE_COLUMNS} FROM nodes WHERE parent_id IS NULL "
            "ORDER BY size_bytes DESC, function_name"
        ).fetchall()
        return [self._row_to_node(r) for r in rows]

    # _name_index used to be a dict[str, list[int]].  core.py only ever
    # called len() on it (for the summary's unique-name count) and iterated
    # _all_nodes once (for top/search).  Expose those same shapes.
    @property
    def _name_index(self) -> "_NameIndexProxy":
        return _NameIndexProxy(self._unique_names)

    @property
    def _all_nodes(self) -> "_AllNodesProxy":
        return _AllNodesProxy(self)

    # ------------------------------------------------------------------
    # Loading
    # ------------------------------------------------------------------

    def reset(self) -> None:
        if self._conn is not None:
            self._conn.close()
        self._conn = None
        self._path = ""
        self.summary = FileSummary()
        self._node_count = 0
        self._root_count = 0
        self._unique_names = 0

    def load_from_file(self, path: str) -> None:
        """Open the SQLite database and validate it.

        Accepts:
          - a .db file produced by `LoliProfilerCLI --dump ... --out X.db`

        Raises RuntimeError if the file doesn't look like a loli .db.
        """
        if not os.path.isfile(path):
            raise FileNotFoundError(f"file not found: {path}")

        self.reset()
        # Open read-only via URI so we never lock the file for writers.
        uri = f"file:{os.path.abspath(path).replace(chr(92), '/')}?mode=ro"
        try:
            conn = sqlite3.connect(uri, uri=True)
        except sqlite3.Error as e:
            raise RuntimeError(f"cannot open {path}: {e}") from e

        # Register a Python-side regex function so search_function can keep
        # the existing regex semantics (re.IGNORECASE, full re syntax).
        conn.create_function("regexp", 2, _regexp_match, deterministic=True)

        # Validate schema by reading metadata.
        try:
            meta = dict(conn.execute("SELECT key, value FROM metadata").fetchall())
        except sqlite3.Error as e:
            conn.close()
            raise RuntimeError(
                f"{path} is not a loli .db (no metadata table): {e}"
            ) from e

        if meta.get("magic") != "loli":
            conn.close()
            raise RuntimeError(f"{path}: not a loli database")
        if meta.get("schema_version") != self.SCHEMA_VERSION:
            conn.close()
            raise RuntimeError(
                f"{path}: schema_version={meta.get('schema_version')} "
                f"(expected {self.SCHEMA_VERSION})"
            )

        self._conn = conn
        self._path = path

        s = FileSummary()
        s.mode = meta.get("mode", "snapshot")
        s.total_allocations = int(meta.get("total_allocations", 0))
        s.total_size = meta.get("total_size_display", "")
        self.summary = s

        self._node_count = int(meta.get("node_count", 0))
        self._root_count = int(meta.get("root_count", 0))
        self._unique_names = int(meta.get("unique_function_names", 0))

    # ------------------------------------------------------------------
    # Internal row -> dataclass
    # ------------------------------------------------------------------

    @staticmethod
    def _row_to_node(row: Tuple) -> TreeNode:
        """Convert a (id, function_name, size_bytes, count, depth) row."""
        node_id, function_name, size_bytes, count, depth = row
        return TreeNode(
            node_id=node_id,
            function_name=function_name,
            size_bytes=size_bytes,
            size_display=format_bytes(size_bytes),
            count=count,
            level=depth,
        )

    # ------------------------------------------------------------------
    # Structured queries (used by both --json mode and the text formatters)
    # ------------------------------------------------------------------

    def node_by_id(self, node_id: int) -> Optional[TreeNode]:
        row = self._conn.execute(
            f"SELECT {_NODE_COLUMNS} FROM nodes WHERE id = ?", (node_id,)
        ).fetchone()
        return self._row_to_node(row) if row is not None else None

    def children_of(self, node_id: int) -> List[TreeNode]:
        rows = self._conn.execute(
            f"SELECT {_NODE_COLUMNS} FROM nodes WHERE parent_id = ? "
            "ORDER BY size_bytes DESC, function_name",
            (node_id,),
        ).fetchall()
        return [self._row_to_node(r) for r in rows]

    def parent_chain(self, node_id: int) -> List[TreeNode]:
        """Return root -> ... -> node, using a recursive CTE walking parent_id."""
        rows = self._conn.execute(
            "WITH RECURSIVE chain(id, function_name, size_bytes, count, depth, parent_id) AS ("
            f"  SELECT {_NODE_COLUMNS}, parent_id FROM nodes WHERE id = ?"
            "  UNION ALL"
            f"  SELECT n.id, n.function_name, n.size_bytes, n.count, n.depth, n.parent_id"
            "  FROM nodes n JOIN chain c ON n.id = c.parent_id"
            ")"
            "SELECT id, function_name, size_bytes, count, depth FROM chain "
            "ORDER BY depth ASC",
            (node_id,),
        ).fetchall()
        return [self._row_to_node(r) for r in rows]

    def top_nodes(self, n: int = 20, min_size_bytes: int = 0) -> List[TreeNode]:
        rows = self._conn.execute(
            f"SELECT {_NODE_COLUMNS} FROM nodes "
            "WHERE size_bytes >= ? "
            "ORDER BY size_bytes DESC, function_name "
            "LIMIT ?",
            (min_size_bytes, n),
        ).fetchall()
        return [self._row_to_node(r) for r in rows]

    def search_nodes(self, pattern: str, max_results: int = 30) -> Tuple[List[TreeNode], int]:
        """Regex search across all function names (case-insensitive).

        Returns (matches, total_count_before_truncation).
        """
        if len(pattern) > 200:
            raise ValueError("pattern too long (max 200 characters)")
        try:
            re.compile(pattern, re.IGNORECASE)
        except re.error as e:
            raise ValueError(f"invalid regex: {e}") from e

        # SQLite's REGEXP operator is wired to our _regexp_match function.
        total_row = self._conn.execute(
            "SELECT COUNT(*) FROM nodes WHERE function_name REGEXP ?",
            (pattern,),
        ).fetchone()
        total = int(total_row[0]) if total_row else 0

        rows = self._conn.execute(
            f"SELECT {_NODE_COLUMNS} FROM nodes "
            "WHERE function_name REGEXP ? "
            "ORDER BY size_bytes DESC, function_name "
            "LIMIT ?",
            (pattern, max_results),
        ).fetchall()
        return [self._row_to_node(r) for r in rows], total

    def iter_subtree(self, root_id: int, max_depth: int) -> Iterator[Tuple[TreeNode, bool]]:
        """Yield (node, was_truncated) in DFS pre-order, sorted size DESC.

        was_truncated=True means the node has children but we stopped descending.
        """
        root = self.node_by_id(root_id)
        if root is None:
            return
        base_depth = root.level
        # Adjust each yielded node's "level" to be relative to the subtree root,
        # matching the previous text formatter's behaviour.
        stack: List[Tuple[TreeNode, int]] = [(root, 0)]
        while stack:
            node, rel_depth = stack.pop()
            kids = self.children_of(node.node_id)
            if rel_depth >= max_depth and kids:
                # Truncate here.
                yield (node, True)
                continue
            yield (node, False)
            # Push in reverse so we pop in size-DESC order.
            for child in reversed(kids):
                stack.append((child, rel_depth + 1))
        _ = base_depth  # suppress unused-warning; kept for future relative-depth use

    def count_descendants(self, node_id: int) -> int:
        row = self._conn.execute(
            "WITH RECURSIVE sub(id) AS ("
            "  SELECT id FROM nodes WHERE parent_id = ?"
            "  UNION ALL"
            "  SELECT n.id FROM nodes n JOIN sub s ON n.parent_id = s.id"
            ")"
            "SELECT COUNT(*) FROM sub",
            (node_id,),
        ).fetchone()
        return int(row[0]) if row else 0

    # ------------------------------------------------------------------
    # Text formatters (one-to-one with the previous tree_model so CLI
    # plain-text output stays identical).
    # ------------------------------------------------------------------

    def get_summary(self) -> str:
        s = self.summary
        lines = [
            "=== Heap Snapshot Summary ===",
            f"Total allocations:      {s.total_allocations:,}",
            f"Total size:             {s.total_size}",
        ]
        lines += [
            "",
            "=== Tree Metadata ===",
            f"Total nodes:            {self._node_count:,}",
            f"Root nodes:             {self._root_count:,}",
            f"Unique function names:  {self._unique_names:,}",
        ]
        roots = self._conn.execute(
            f"SELECT {_NODE_COLUMNS} FROM nodes WHERE parent_id IS NULL "
            "ORDER BY size_bytes DESC, function_name LIMIT 5"
        ).fetchall()
        if roots:
            lines.append("")
            lines.append("=== Top Root Nodes ===")
            for r in roots:
                n = self._row_to_node(r)
                lines.append(f"  [{n.node_id}] {n.function_name}, "
                             f"{n.size_display}, count={n.count}")
        return "\n".join(lines)

    def get_top_allocations(self, n: int = 20, min_size_mb: float = 0.0) -> str:
        min_bytes = int(min_size_mb * 1024 * 1024)
        nodes = self.top_nodes(n=n, min_size_bytes=min_bytes)
        if not nodes:
            return f"No nodes found with size >= {min_size_mb} MB"
        lines = [f"Top {len(nodes)} allocations (min {min_size_mb} MB):", ""]
        for nd in nodes:
            lines.append(
                f"  [{nd.node_id}] {nd.size_display}, count={nd.count}, "
                f"depth={nd.level} | {nd.function_name}"
            )
        return "\n".join(lines)

    def get_children(self, node_id: int) -> str:
        parent = self.node_by_id(node_id)
        if parent is None:
            return f"Error: node_id {node_id} not found"
        kids = self.children_of(node_id)
        if not kids:
            return f"[{node_id}] {parent.function_name} has no children (leaf node)"
        lines = [
            f"Children of [{node_id}] {parent.function_name} ({len(kids)} children):",
            "",
        ]
        for c in kids:
            lines.append(
                f"  [{c.node_id}] {c.size_display}, count={c.count} "
                f"| {c.function_name}"
            )
        return "\n".join(lines)

    def get_call_path(self, node_id: int) -> str:
        if self.node_by_id(node_id) is None:
            return f"Error: node_id {node_id} not found"
        chain = self.parent_chain(node_id)
        lines = [f"Call path to [{node_id}] ({len(chain)} frames):", ""]
        for i, n in enumerate(chain):
            indent = "  " * i
            lines.append(
                f"{indent}[{n.node_id}] {n.function_name}, "
                f"{n.size_display}, count={n.count}"
            )
        return "\n".join(lines)

    def search_function(self, pattern: str, max_results: int = 30) -> str:
        try:
            matches, total = self.search_nodes(pattern, max_results=max_results)
        except ValueError as e:
            msg = str(e)
            if msg.startswith("pattern too long"):
                return "Error: " + msg
            return f"Invalid regex pattern: {msg.removeprefix('invalid regex: ')}"

        if not matches:
            return f"No functions matching '{pattern}'"

        header = f"Found {len(matches)} matches"
        if total > len(matches):
            header += f" (showing top {max_results} of {total} total)"
        header += f" for '{pattern}':"
        lines = [header, ""]
        for nd in matches:
            lines.append(
                f"  [{nd.node_id}] {nd.size_display}, count={nd.count}, "
                f"depth={nd.level} | {nd.function_name}"
            )
        return "\n".join(lines)

    def get_subtree(self, node_id: int, max_depth: int = 4) -> str:
        if self.node_by_id(node_id) is None:
            return f"Error: node_id {node_id} not found"

        # Build the indented view by hand using a depth-aware DFS.  We
        # need each line to know its relative depth, so iter_subtree's
        # bare yield isn't enough — re-implement the walk here.
        lines: List[str] = []
        truncated = 0

        # Stack frames are (node, relative_depth).
        root = self.node_by_id(node_id)
        if root is None:
            return f"Error: node_id {node_id} not found"
        stack: List[Tuple[TreeNode, int]] = [(root, 0)]
        while stack:
            node, depth = stack.pop()
            indent = "    " * depth
            kids = self.children_of(node.node_id)
            if depth >= max_depth and kids:
                truncated += self.count_descendants(node.node_id)
                lines.append(
                    f"{indent}[{node.node_id}] {node.function_name}, "
                    f"{node.size_display}, count={node.count}"
                )
                lines.append(f"{indent}    ... ({len(kids)} children truncated)")
                continue
            lines.append(
                f"{indent}[{node.node_id}] {node.function_name}, "
                f"{node.size_display}, count={node.count}"
            )
            for child in reversed(kids):
                stack.append((child, depth + 1))

        if truncated:
            lines.append(
                f"\n({truncated} descendant nodes truncated at depth {max_depth})"
            )
        return "\n".join(lines)


# ---------------------------------------------------------------------------
# Helpers / proxies
# ---------------------------------------------------------------------------

def _regexp_match(pattern: str, value: Optional[str]) -> int:
    """SQLite REGEXP() implementation (case-insensitive, full Python re).

    Returns 1/0 because SQLite booleans are integers.  None values never match.
    """
    if value is None or pattern is None:
        return 0
    try:
        return 1 if re.search(pattern, value, re.IGNORECASE) else 0
    except re.error:
        return 0


class _NameIndexProxy:
    """Stand-in for the old _name_index dict — only len() was ever called."""
    __slots__ = ("_n",)

    def __init__(self, n: int) -> None:
        self._n = n

    def __len__(self) -> int:
        return self._n


class _AllNodesProxy:
    """Stand-in for the old _all_nodes dict.

    Old code did two things:
      1. `db._all_nodes.get(node_id)` -> TreeNode | None
      2. `db._all_nodes.values()` -> iterable of every TreeNode

    Both still work, backed by SQL.  values() iterates all 1.5M rows and
    is currently used only by core.py's --json mode for `top` and `search`;
    those code paths now have dedicated DB queries in core.py, so values()
    will become an unused convenience.  We keep it functional for safety.
    """
    __slots__ = ("_db",)

    def __init__(self, db: CallTreeDatabase) -> None:
        self._db = db

    def get(self, node_id: int) -> Optional[TreeNode]:
        return self._db.node_by_id(node_id)

    def values(self) -> Iterator[TreeNode]:
        rows = self._db._conn.execute(
            f"SELECT {_NODE_COLUMNS} FROM nodes"
        )
        for row in rows:
            yield CallTreeDatabase._row_to_node(row)

    def __contains__(self, node_id: object) -> bool:
        if not isinstance(node_id, int):
            return False
        return self._db.node_by_id(node_id) is not None


if __name__ == '__main__':
    import sys
    if len(sys.argv) < 2:
        print("Usage: python tree_model.py <snapshot.db>")
        sys.exit(1)
    db = CallTreeDatabase()
    db.load_from_file(sys.argv[1])
    print(f"Roots: {db._root_count}, Total nodes: {db.node_count}")
    print()
    print(db.get_summary())
