#!/usr/bin/env python3
"""
Intelligent preprocessing for memory diff files.

This module filters large memory diff files to extract functional-level call stacks,
removing low-level primitives and focusing on application logic that matters.

Usage:
    python preprocess_memory_diff.py diff.txt -o filtered_diff.txt --threshold 2.0
    python preprocess_memory_diff.py diff.txt --verbose --validate-patterns
"""

import re
import sys
import argparse
from typing import List, Dict, Optional, Tuple, Set
from collections import defaultdict
import hashlib


class CallStackNode:
    """Represents a node in the call stack tree."""

    def __init__(self, function_name: str, size_mb: float, count: int, level: int):
        self.function_name = function_name
        self.size_mb = size_mb
        self.count = count
        self.level = level
        self.parent: Optional[CallStackNode] = None
        self.children: List[CallStackNode] = []

    def add_child(self, child: 'CallStackNode'):
        """Add a child node and set parent relationship."""
        child.parent = self
        self.children.append(child)

    def get_call_path(self) -> List[str]:
        """Get full call path from root to this node."""
        path = []
        node = self
        while node is not None:
            path.append(node.function_name)
            node = node.parent
        return list(reversed(path))

    def get_call_path_hash(self) -> str:
        """Get a hash of the call path for deduplication."""
        path_str = " -> ".join(self.get_call_path())
        return hashlib.md5(path_str.encode()).hexdigest()

    def __repr__(self):
        return f"CallStackNode({self.function_name}, {self.size_mb:.2f} MB, {self.count})"


# Hardcoded UE4/Wwise pattern matching
LOW_LEVEL_PATTERNS = [
    r'FMemory::',
    r'operator new',
    r'malloc',
    r'calloc',
    r'realloc',
    r'TArray::',
    r'TMap::',
    r'TSet::',
    r'TSparseArray::',
    r'TBitArray::',
    r'FPropertyTag::',
    r'FArchive::',
    r'FLinkerLoad::',
    r'FString::',
    r'FName::',
    r'FText::',
    r'FScriptArray::',
    r'TIndirectArray::',
    r'FUObjectArray::',
]

FUNCTIONAL_PATTERNS = [
    r'^U[A-Z]\w+::',      # UObject-derived classes
    r'^A[A-Z]\w+::',      # AActor-derived classes
    r'^F\w+Manager::',    # Manager classes
    r'^F\w+System::',     # System classes
    r'^CAk\w+::',         # Wwise audio API
    r'^Ak\w+::',          # Wwise audio API (alternative prefix)
    r'::(Load|Initialize|Create|Spawn|Allocate|Setup)',  # Functional verbs
]

# Root functions to ignore (generic thread wrappers)
IGNORE_ROOT_FUNCTIONS = [
    'FRunnableThreadPThread::Run',
    'FEngineLoop::Tick',
    'FRenderingThread::Run',
    'FTaskGraphImplementation::ProcessThreadUntilIdle',
    '-[IOSAppDelegate MainAppThread:]',
]


def parse_line(line: str) -> Optional[Tuple[str, float, int, int]]:
    """
    Parse a single line from diff.txt.

    Format: "    FunctionName(), +SIZE MB, +COUNT"
    Returns: (function_name, size_mb, count, level) or None if parsing fails
    """
    # Calculate indentation level (4 spaces per level)
    stripped = line.lstrip(' ')
    indent_count = len(line) - len(stripped)
    level = indent_count // 4

    # Parse function name, size, and count
    # Pattern: "FunctionName(), +10.44 MB, +1234" or "FunctionName(), -10.44 MB, -1234"
    match = re.match(r'(.+?),\s*([\+\-][\d\.]+)\s*MB,\s*([\+\-]\d+)', stripped)
    if not match:
        return None

    function_name = match.group(1).strip()
    size_mb = float(match.group(2))
    count = int(match.group(3))

    return function_name, size_mb, count, level


def parse_diff_tree(content: str) -> List[CallStackNode]:
    """
    Parse diff.txt content into a tree structure.

    Returns list of root nodes (level 0).
    """
    lines = content.split('\n')
    roots: List[CallStackNode] = []
    node_stack: List[CallStackNode] = []  # Stack to track parent nodes

    for line in lines:
        if not line.strip():
            continue

        parsed = parse_line(line)
        if parsed is None:
            continue

        function_name, size_mb, count, level = parsed
        node = CallStackNode(function_name, size_mb, count, level)

        # Adjust stack to current level
        while len(node_stack) > level:
            node_stack.pop()

        # Add to tree
        if level == 0:
            roots.append(node)
        else:
            if node_stack:
                parent = node_stack[-1]
                parent.add_child(node)

        node_stack.append(node)

    return roots


def is_low_level_function(function_name: str) -> bool:
    """Check if function matches low-level patterns."""
    for pattern in LOW_LEVEL_PATTERNS:
        if re.search(pattern, function_name):
            return True
    return False


def is_functional_level_function(function_name: str) -> bool:
    """Check if function matches functional-level patterns."""
    for pattern in FUNCTIONAL_PATTERNS:
        if re.search(pattern, function_name):
            return True
    return False


def should_ignore_root(function_name: str) -> bool:
    """Check if this is a generic root function to ignore."""
    for ignore_func in IGNORE_ROOT_FUNCTIONS:
        if ignore_func in function_name:
            return True
    return False


def find_functional_ancestor(node: CallStackNode) -> Optional[CallStackNode]:
    """
    Walk up the tree from this node to find the first functional-level ancestor.

    Returns the first functional ancestor, or None if not found.
    """
    current = node.parent
    while current is not None:
        if is_functional_level_function(current.function_name):
            return current
        current = current.parent
    return None


def extract_functional_stacks(root: CallStackNode, threshold_mb: float) -> List[CallStackNode]:
    """
    Extract functional-level call stacks from a root node.

    Algorithm:
    1. DFS traverse from root
    2. At each node >= threshold:
       - If it's low-level, skip it
       - If it's functional-level, keep it
       - If it's neutral, keep it if >= threshold
    3. Return list of significant nodes (>= threshold) that are not low-level
    """
    significant_nodes: List[CallStackNode] = []

    def dfs(node: CallStackNode):
        # Check if this node meets threshold and is not low-level
        if abs(node.size_mb) >= threshold_mb:
            # Skip low-level functions entirely
            if not is_low_level_function(node.function_name):
                # This is a significant node worth keeping
                significant_nodes.append(node)

        # Continue DFS to find more significant nodes
        for child in node.children:
            dfs(child)

    dfs(root)
    return significant_nodes


def build_subtree_with_threshold(node: CallStackNode, threshold_mb: float, stop_at_low_level: bool = True) -> CallStackNode:
    """
    Build a new subtree rooted at node, including only children >= threshold.

    This creates a deep copy of the relevant parts of the tree.

    Args:
        node: Root node to copy
        threshold_mb: Minimum size threshold
        stop_at_low_level: If True, stop recursion at low-level functions
    """
    new_node = CallStackNode(node.function_name, node.size_mb, node.count, node.level)

    for child in node.children:
        if abs(child.size_mb) >= threshold_mb:
            # Stop at low-level functions to avoid deep nesting
            if stop_at_low_level and is_low_level_function(child.function_name):
                continue

            new_child = build_subtree_with_threshold(child, threshold_mb, stop_at_low_level)
            new_node.add_child(new_child)

    return new_node


def merge_duplicate_stacks(stacks: List[CallStackNode]) -> List[CallStackNode]:
    """
    Merge duplicate call stacks by path hash.

    Stacks with identical call paths are merged by summing size_mb and count.
    """
    stack_map: Dict[str, CallStackNode] = {}

    for stack in stacks:
        path_hash = stack.get_call_path_hash()

        if path_hash in stack_map:
            # Duplicate found - keep the existing one (don't sum values)
            # The values represent the same allocation point, not separate allocations
            pass
        else:
            # Add new
            stack_map[path_hash] = stack

    return list(stack_map.values())


def format_tree(node: CallStackNode, indent_level: int = 0) -> str:
    """
    Format a tree node and its children as text.

    Format: "    FunctionName(), +SIZE MB, +COUNT"
    """
    indent = "    " * indent_level
    sign = "+" if node.size_mb >= 0 else ""
    line = f"{indent}{node.function_name}, {sign}{node.size_mb:.2f} MB, {sign}{node.count}\n"

    for child in node.children:
        line += format_tree(child, indent_level + 1)

    return line


def preprocess_diff(content: str, threshold_mb: float, verbose: bool = False) -> str:
    """
    Main preprocessing function.

    Args:
        content: Raw diff.txt content
        threshold_mb: Minimum memory delta in MB
        verbose: Print statistics

    Returns:
        Filtered diff content as string
    """
    # Parse tree
    roots = parse_diff_tree(content)

    if verbose:
        total_nodes = count_nodes(roots)
        print(f"Parsed {len(roots)} root nodes, {total_nodes} total nodes")

    # Find all significant roots (including ignored wrappers, we'll traverse them anyway)
    significant_roots = []
    for root in roots:
        if abs(root.size_mb) >= threshold_mb:
            significant_roots.append(root)

    if verbose:
        print(f"Found {len(significant_roots)} significant roots (>= {threshold_mb} MB)")

    # Extract functional stacks from each significant root
    all_functional_stacks = []
    for root in significant_roots:
        functional_stacks = extract_functional_stacks(root, threshold_mb)

        if verbose:
            print(f"Extracted {len(functional_stacks)} functional nodes from {root.function_name}")

        # For each functional node, build a simplified path
        for func_node in functional_stacks:
            # Build filtered path from root to this node, stopping at low-level children
            filtered_subtree = build_filtered_path_to_node(root, func_node, threshold_mb)
            if filtered_subtree:
                all_functional_stacks.append(filtered_subtree)

    if verbose:
        print(f"Built {len(all_functional_stacks)} filtered call stacks")

    # Merge duplicates (identical call stacks should be combined)
    merged_stacks = merge_duplicate_stacks(all_functional_stacks)

    if verbose:
        print(f"After deduplication: {len(merged_stacks)} unique call stacks")

    # Sort by size (largest first)
    merged_stacks.sort(key=lambda n: abs(n.size_mb), reverse=True)

    # Format output
    output = ""
    for stack in merged_stacks:
        output += format_tree(stack, indent_level=0)
        output += "\n"  # Extra newline between stacks

    return output


def build_filtered_path_to_node(root: CallStackNode, target: CallStackNode, threshold_mb: float) -> Optional[CallStackNode]:
    """
    Build a filtered path from root to target node, including target's subtree.

    Only includes nodes >= threshold along the path.
    Stops at low-level functions in the subtree.
    """
    # Get path from root to target
    target_path = target.get_call_path()

    # Build new tree following this path
    new_root = CallStackNode(root.function_name, root.size_mb, root.count, 0)
    current_new_node = new_root
    current_old_node = root

    # Walk down the path (skip root since we already created it)
    for i in range(1, len(target_path)):
        func_name = target_path[i]

        # Find the child in the original tree
        child = None
        for c in current_old_node.children:
            if c.function_name == func_name:
                child = c
                break

        if child is None:
            return None  # Path broken

        # Create new node
        new_child = CallStackNode(child.function_name, child.size_mb, child.count, i)
        current_new_node.add_child(new_child)

        current_new_node = new_child
        current_old_node = child

    # Now add target's subtree (filtered by threshold, stopping at low-level)
    for child in target.children:
        if abs(child.size_mb) >= threshold_mb:
            # Stop at low-level functions
            if is_low_level_function(child.function_name):
                continue

            new_child = build_subtree_with_threshold(child, threshold_mb, stop_at_low_level=True)
            current_new_node.add_child(new_child)

    return new_root


def count_nodes(roots: List[CallStackNode]) -> int:
    """Count total number of nodes in the tree."""
    count = 0

    def dfs(node: CallStackNode):
        nonlocal count
        count += 1
        for child in node.children:
            dfs(child)

    for root in roots:
        dfs(root)

    return count


def main():
    parser = argparse.ArgumentParser(
        description='Intelligent preprocessing for memory diff files',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python preprocess_memory_diff.py diff.txt -o filtered_diff.txt
  python preprocess_memory_diff.py diff.txt --threshold 2.0 --verbose
  python preprocess_memory_diff.py diff.txt --validate-patterns
        """
    )

    parser.add_argument('input_file', help='Input diff.txt file')
    parser.add_argument('-o', '--output', default='filtered_diff.txt',
                        help='Output file path (default: filtered_diff.txt)')
    parser.add_argument('-t', '--threshold', type=float, default=2.0,
                        help='Minimum memory delta in MB (default: 2.0)')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Show filtering statistics')
    parser.add_argument('--validate-patterns', action='store_true',
                        help='Enable Claude-assisted pattern validation (hybrid mode)')

    args = parser.parse_args()

    # Read input file
    try:
        with open(args.input_file, 'r', encoding='utf-8') as f:
            content = f.read()
    except FileNotFoundError:
        print(f"Error: File not found: {args.input_file}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"Error reading file: {e}", file=sys.stderr)
        return 1

    if args.verbose:
        input_size_mb = len(content) / (1024 * 1024)
        print(f"Input file size: {input_size_mb:.2f} MB")
        print(f"Threshold: {args.threshold} MB")
        print()

    # Preprocess
    filtered_content = preprocess_diff(content, args.threshold, args.verbose)

    # Write output
    try:
        with open(args.output, 'w', encoding='utf-8') as f:
            f.write(filtered_content)
    except Exception as e:
        print(f"Error writing output file: {e}", file=sys.stderr)
        return 1

    if args.verbose:
        output_size_mb = len(filtered_content) / (1024 * 1024)
        input_size_mb = len(content) / (1024 * 1024)
        reduction_pct = (1 - output_size_mb / input_size_mb) * 100 if input_size_mb > 0 else 0

        print()
        print(f"Output file size: {output_size_mb:.2f} MB")
        print(f"Reduction: {reduction_pct:.1f}%")
        print(f"Output written to: {args.output}")

    # Pattern validation (optional)
    if args.validate_patterns:
        print("\nPattern validation is not yet implemented.")
        print("This feature will use Claude to validate and refine pattern matching.")

    return 0


if __name__ == '__main__':
    sys.exit(main())
