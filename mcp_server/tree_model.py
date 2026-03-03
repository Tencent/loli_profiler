#!/usr/bin/env python3
"""
Data model and query engine for LoliProfiler heap data files.

Parses the tree-structured output from `LoliProfilerCLI --compare` (diff) or
`LoliProfilerCLI --dump` (snapshot) into an indexed in-memory tree and exposes
query methods for interactive exploration.
"""

import re
from dataclasses import dataclass, field
from typing import List, Optional, Dict, Tuple


@dataclass
class TreeNode:
    """A single node in the call stack tree."""
    node_id: int
    function_name: str
    size_bytes: int           # Normalized to bytes (signed for diffs, unsigned for snapshots)
    size_display: str         # Original string like "+10.44 MB" or "10.44 MB"
    count: int                # Allocation count delta (signed)
    level: int                # Indentation level (0 = root)
    parent: Optional['TreeNode'] = field(default=None, repr=False)
    children: List['TreeNode'] = field(default_factory=list, repr=False)

    def __repr__(self):
        return f"TreeNode(id={self.node_id}, {self.function_name}, {self.size_display}, count={self.count})"


@dataclass
class FileSummary:
    """Header statistics from the report file (diff or snapshot)."""
    mode: str = ""  # "diff" or "snapshot", auto-detected from file header

    # Shared (maps to "Total allocations"/"Baseline allocations" and similar)
    total_allocations: int = 0
    total_size: str = ""

    # Diff-only fields
    comparison_allocations: int = 0
    comparison_total_size: str = ""
    size_delta: str = ""
    changed_allocations: int = 0
    new_allocations: int = 0


# Regex for size values with units: "+10.44 MB", "-925.03 KB", "+1.50 Bytes", "+2.31 GB"
_SIZE_RE = re.compile(r'([\+\-]?[\d\.]+)\s*(GB|MB|KB|Bytes?)', re.IGNORECASE)

# Regex for a tree line: "FunctionName(), +SIZE UNIT, +COUNT"
# Uses greedy (.*) for function name to handle commas in C++ template names,
# anchored by the ", SIZE, COUNT" suffix at end of line.
# Sign is optional to handle zero-size nodes ("0 Bytes").
_LINE_RE = re.compile(
    r'^(.*),\s*([\+\-]?[\d.]+\s*(?:GB|MB|KB|Bytes?))\s*,\s*([\+\-]?\d+)\s*$',
    re.IGNORECASE
)

# Header parsing patterns — field name → regex
# Snapshot headers ("Total allocations", "Total size") map to the shared fields.
# Diff headers ("Baseline allocations", "Baseline total size") also map to the
# shared fields so the same FileSummary works for both formats.
_HEADER_PATTERNS = {
    # Snapshot-specific
    'total_allocations': re.compile(r'^Total allocations:\s*(\d+)'),
    'total_size': re.compile(r'^Total size:\s*(.+)'),
    # Diff-specific (baseline → shared fields)
    'baseline_allocations': re.compile(r'^Baseline allocations:\s*(\d+)'),
    'baseline_total_size': re.compile(r'^Baseline total size:\s*(.+)'),
    # Diff-only
    'comparison_allocations': re.compile(r'^Comparison allocations:\s*(\d+)'),
    'comparison_total_size': re.compile(r'^Comparison total size:\s*(.+)'),
    'size_delta': re.compile(r'^Size delta:\s*(.+)'),
    'changed_allocations': re.compile(r'^Changed allocations.*?:\s*(\d+)'),
    'new_allocations': re.compile(r'^New allocations.*?:\s*(\d+)'),
}

# Map diff-specific field names to their FileSummary attribute names
_HEADER_FIELD_MAP = {
    'baseline_allocations': 'total_allocations',
    'baseline_total_size': 'total_size',
}


def parse_size_to_bytes(size_str: str) -> int:
    """Convert a size string like '+10.44 MB' to signed bytes."""
    m = _SIZE_RE.search(size_str)
    if not m:
        return 0
    value = float(m.group(1))
    unit = m.group(2).upper()
    multipliers = {'BYTES': 1, 'BYTE': 1, 'KB': 1024, 'MB': 1024**2, 'GB': 1024**3}
    return int(value * multipliers.get(unit, 1))


def format_bytes(n: int, signed: bool = True) -> str:
    """Format byte count to human-readable string.

    Args:
        n: Byte count (may be negative for diffs).
        signed: If True, prepend +/- prefix (diff mode).
                If False, use absolute value with no sign (snapshot mode).
    """
    if n == 0:
        return "0 Bytes"
    if signed:
        sign = "+" if n > 0 else "-"
    else:
        sign = ""
    a = abs(n)
    if a >= 1024**3:
        return f"{sign}{a / 1024**3:.2f} GB"
    if a >= 1024**2:
        return f"{sign}{a / 1024**2:.2f} MB"
    if a >= 1024:
        return f"{sign}{a / 1024:.2f} KB"
    return f"{sign}{a} Bytes"


class CallTreeDatabase:
    """Indexed in-memory database of a heap data tree, with query methods."""

    def __init__(self):
        self.roots: List[TreeNode] = []
        self.summary: FileSummary = FileSummary()
        self._all_nodes: Dict[int, TreeNode] = {}
        self._name_index: Dict[str, List[int]] = {}  # lowercase name -> node_ids
        self._next_id: int = 0

    # ------------------------------------------------------------------
    # Properties
    # ------------------------------------------------------------------

    @property
    def node_count(self) -> int:
        """Total number of nodes in the tree."""
        return len(self._all_nodes)

    # ------------------------------------------------------------------
    # Loading
    # ------------------------------------------------------------------

    def reset(self) -> None:
        """Clear all loaded data, returning to empty state."""
        self.roots.clear()
        self.summary = FileSummary()
        self._all_nodes.clear()
        self._name_index.clear()
        self._next_id = 0

    def load_from_file(self, path: str) -> None:
        with open(path, 'r', encoding='utf-8') as f:
            content = f.read()
        self._parse(content)

    def load_from_string(self, content: str) -> None:
        self._parse(content)

    def _parse(self, content: str) -> None:
        lines = content.split('\n')
        header_done = False
        node_stack: List[TreeNode] = []  # stack tracking parent hierarchy

        for line in lines:
            raw = line.rstrip('\r')

            # Blank line
            if not raw.strip():
                continue

            # Try header parsing until we hit tree data
            if not header_done:
                if self._try_parse_header(raw.strip()):
                    continue
                # Section markers signal end of header
                if raw.strip().startswith('=== Memory Growth') or raw.strip().startswith('=== Memory Allocations'):
                    header_done = True
                    continue
                # Other non-parseable header lines (like "=== LoliProfiler...")
                if raw.strip().startswith('==='):
                    continue

            # Parse tree line
            stripped = raw.lstrip(' ')
            indent = len(raw) - len(stripped)
            level = indent // 4

            m = _LINE_RE.match(stripped)
            if not m:
                # Could still be a header line that slipped through
                if not header_done:
                    self._try_parse_header(raw.strip())
                continue

            header_done = True  # If we parsed a tree line, header is definitely done

            func_name = m.group(1).strip()
            size_display = m.group(2).strip()
            count = int(m.group(3))
            size_bytes = parse_size_to_bytes(size_display)

            node = TreeNode(
                node_id=self._next_id,
                function_name=func_name,
                size_bytes=size_bytes,
                size_display=size_display,
                count=count,
                level=level,
            )
            self._next_id += 1
            self._all_nodes[node.node_id] = node

            # Index by name
            key = func_name.lower()
            if key not in self._name_index:
                self._name_index[key] = []
            self._name_index[key].append(node.node_id)

            # Build tree hierarchy
            while len(node_stack) > level:
                node_stack.pop()

            if level == 0:
                self.roots.append(node)
            elif node_stack:
                parent = node_stack[-1]
                parent.children.append(node)
                node.parent = parent

            node_stack.append(node)

    def _try_parse_header(self, line: str) -> bool:
        # Auto-detect file format from the title line
        if line == '=== LoliProfiler Profile Report ===':
            self.summary.mode = "snapshot"
            return True
        if line == '=== LoliProfiler Comparison Report ===':
            self.summary.mode = "diff"
            return True

        for field_name, pattern in _HEADER_PATTERNS.items():
            m = pattern.search(line)
            if m:
                val = m.group(1).strip()
                # Map diff-specific names (e.g. baseline_allocations) to FileSummary attrs
                attr = _HEADER_FIELD_MAP.get(field_name, field_name)
                if attr in ('total_allocations', 'comparison_allocations',
                            'changed_allocations', 'new_allocations'):
                    setattr(self.summary, attr, int(val))
                else:
                    setattr(self.summary, attr, val)
                return True
        return False

    # ------------------------------------------------------------------
    # Query methods
    # ------------------------------------------------------------------

    def get_summary(self) -> str:
        """Return header stats and tree metadata as formatted text."""
        s = self.summary
        if s.mode == "snapshot":
            lines = [
                "=== Heap Snapshot Summary ===",
                f"Total allocations:      {s.total_allocations:,}",
                f"Total size:             {s.total_size}",
            ]
        else:
            lines = [
                "=== Heap Diff Summary ===",
                f"Baseline allocations:   {s.total_allocations:,}",
                f"Comparison allocations:  {s.comparison_allocations:,}",
                f"Baseline total size:    {s.total_size}",
                f"Comparison total size:  {s.comparison_total_size}",
                f"Size delta:             {s.size_delta}",
                f"Changed allocations:    {s.changed_allocations:,}",
                f"New allocations:        {s.new_allocations:,}",
            ]

        lines += [
            "",
            "=== Tree Metadata ===",
            f"Total nodes:            {len(self._all_nodes):,}",
            f"Root nodes:             {len(self.roots):,}",
            f"Unique function names:  {len(self._name_index):,}",
        ]

        # Top 5 root nodes by size
        sorted_roots = sorted(self.roots, key=lambda n: abs(n.size_bytes), reverse=True)[:5]
        if sorted_roots:
            lines.append("")
            lines.append("=== Top Root Nodes ===")
            for n in sorted_roots:
                lines.append(f"  [{n.node_id}] {n.function_name}, {n.size_display}, count={n.count}")

        return "\n".join(lines)

    def get_top_allocations(self, n: int = 20, min_size_mb: float = 0.0) -> str:
        """Return top N nodes by absolute size across all depths."""
        min_bytes = int(min_size_mb * 1024 * 1024)
        candidates = [
            node for node in self._all_nodes.values()
            if abs(node.size_bytes) >= min_bytes
        ]
        candidates.sort(key=lambda nd: abs(nd.size_bytes), reverse=True)
        candidates = candidates[:n]

        if not candidates:
            return f"No nodes found with size >= {min_size_mb} MB"

        lines = [f"Top {len(candidates)} allocations (min {min_size_mb} MB):"]
        lines.append("")
        for node in candidates:
            depth_label = f"depth={node.level}"
            lines.append(
                f"  [{node.node_id}] {node.size_display}, count={node.count}, "
                f"{depth_label} | {node.function_name}"
            )
        return "\n".join(lines)

    def get_children(self, node_id: int) -> str:
        """Return direct children of a node, sorted by abs(size) descending."""
        node = self._all_nodes.get(node_id)
        if node is None:
            return f"Error: node_id {node_id} not found"

        if not node.children:
            return f"[{node_id}] {node.function_name} has no children (leaf node)"

        sorted_children = sorted(node.children, key=lambda c: abs(c.size_bytes), reverse=True)
        lines = [
            f"Children of [{node_id}] {node.function_name} ({len(sorted_children)} children):",
            ""
        ]
        for child in sorted_children:
            lines.append(
                f"  [{child.node_id}] {child.size_display}, count={child.count} "
                f"| {child.function_name}"
            )
        return "\n".join(lines)

    def get_call_path(self, node_id: int) -> str:
        """Return the root-to-node chain with all intermediate nodes."""
        node = self._all_nodes.get(node_id)
        if node is None:
            return f"Error: node_id {node_id} not found"

        path: List[TreeNode] = []
        current = node
        while current is not None:
            path.append(current)
            current = current.parent
        path.reverse()

        lines = [f"Call path to [{node_id}] ({len(path)} frames):"]
        lines.append("")
        for i, n in enumerate(path):
            indent = "  " * i
            lines.append(
                f"{indent}[{n.node_id}] {n.function_name}, {n.size_display}, count={n.count}"
            )
        return "\n".join(lines)

    def search_function(self, pattern: str, max_results: int = 30) -> str:
        """Regex search across all function names. Returns matching nodes."""
        if len(pattern) > 200:
            return "Error: pattern too long (max 200 characters)"
        try:
            regex = re.compile(pattern, re.IGNORECASE)
        except re.error as e:
            return f"Invalid regex pattern: {e}"

        matches = [
            node for node in self._all_nodes.values()
            if regex.search(node.function_name)
        ]

        if not matches:
            return f"No functions matching '{pattern}'"

        # Sort by abs size descending to always return the top results
        matches.sort(key=lambda nd: abs(nd.size_bytes), reverse=True)

        total = len(matches)
        matches = matches[:max_results]

        header = f"Found {len(matches)} matches"
        if total > len(matches):
            header += f" (showing top {max_results} of {total} total)"
        header += f" for '{pattern}':"

        lines = [header, ""]
        for node in matches:
            lines.append(
                f"  [{node.node_id}] {node.size_display}, count={node.count}, "
                f"depth={node.level} | {node.function_name}"
            )
        return "\n".join(lines)

    def get_subtree(self, node_id: int, max_depth: int = 4) -> str:
        """Return indented text view of the subtree below a node."""
        node = self._all_nodes.get(node_id)
        if node is None:
            return f"Error: node_id {node_id} not found"

        lines: List[str] = []
        truncated = [0]  # mutable counter

        def _walk(n: TreeNode, depth: int):
            indent = "    " * depth
            lines.append(
                f"{indent}[{n.node_id}] {n.function_name}, {n.size_display}, count={n.count}"
            )
            if depth >= max_depth and n.children:
                truncated[0] += self._count_descendants(n)
                lines.append(f"{indent}    ... ({len(n.children)} children truncated)")
                return
            for child in sorted(n.children, key=lambda c: abs(c.size_bytes), reverse=True):
                _walk(child, depth + 1)

        _walk(node, 0)

        if truncated[0]:
            lines.append(f"\n({truncated[0]} descendant nodes truncated at depth {max_depth})")
        return "\n".join(lines)

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _count_descendants(self, node: TreeNode) -> int:
        count = 0
        stack = list(node.children)
        while stack:
            child = stack.pop()
            count += 1
            stack.extend(child.children)
        return count


if __name__ == '__main__':
    import sys
    if len(sys.argv) < 2:
        print("Usage: python tree_model.py <diff.txt|snapshot.txt>")
        sys.exit(1)
    db = CallTreeDatabase()
    db.load_from_file(sys.argv[1])
    print(f"Roots: {len(db.roots)}, Total nodes: {db.node_count}")
    print()
    print(db.get_summary())
