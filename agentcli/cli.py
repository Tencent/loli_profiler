"""agentcli.cli — argparse subcommand dispatcher for the `loli` CLI.

Each subcommand is a thin wrapper around agentcli.core. Output is plain
text by default. Pass --json to any subcommand for a structured JSON
response.

Discoverability for agents: `loli describe` (or `loli describe <cmd>`)
returns the full schema as JSON.
"""
from __future__ import annotations

import argparse
import json
import sys

from agentcli import core


# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------

def _emit(result: dict, as_json: bool) -> int:
    """Emit a {"text": ..., "data": ...} result. Returns the exit code."""
    if as_json:
        print(json.dumps(result.get("data", {}), indent=2, ensure_ascii=False, default=str))
    else:
        print(result.get("text", ""))

    data = result.get("data", {})
    if isinstance(data, dict) and "error" in data:
        return 1
    if isinstance(result.get("text", ""), str) and result["text"].startswith("Error:"):
        return 1
    return 0


def _emit_describe(result: dict) -> int:
    """describe always emits JSON (it's an introspection tool)."""
    print(json.dumps(result, indent=2, ensure_ascii=False, default=str))
    return 1 if isinstance(result, dict) and "error" in result else 0


# ---------------------------------------------------------------------------
# Subcommand handlers
# ---------------------------------------------------------------------------

def cmd_load_file(args: argparse.Namespace) -> int:
    return _emit(core.load_file(args.file_path), args.json)


def cmd_summary(args: argparse.Namespace) -> int:
    return _emit(core.summary(args.file_path), args.json)


def cmd_top(args: argparse.Namespace) -> int:
    return _emit(core.top(args.file_path, n=args.n, min_size_mb=args.min_size_mb), args.json)


def cmd_children(args: argparse.Namespace) -> int:
    return _emit(core.children(args.file_path, node_id=args.node_id), args.json)


def cmd_call_path(args: argparse.Namespace) -> int:
    return _emit(core.call_path(args.file_path, node_id=args.node_id), args.json)


def cmd_search(args: argparse.Namespace) -> int:
    return _emit(core.search(args.file_path, pattern=args.pattern,
                             max_results=args.max), args.json)


def cmd_subtree(args: argparse.Namespace) -> int:
    return _emit(core.subtree(args.file_path, node_id=args.node_id,
                              max_depth=args.max_depth), args.json)


def cmd_describe(args: argparse.Namespace) -> int:
    return _emit_describe(core.describe(args.command))


# ---------------------------------------------------------------------------
# Argument parser
# ---------------------------------------------------------------------------

def _add_file_arg(p: argparse.ArgumentParser) -> None:
    p.add_argument("file_path",
                   help="Path to .loli (auto-converted to .db) or .db heap data file")


def _add_json_flag(p: argparse.ArgumentParser) -> None:
    p.add_argument("--json", action="store_true",
                   help="Emit machine-readable JSON instead of formatted text")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="loli",
        description=(
            "CLI for LoliProfiler heap data exploration.\n\n"
            "Run `loli describe` to print the full command schema as JSON "
            "(useful for agents). Run `loli <cmd> --help` for per-command help."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = p.add_subparsers(dest="cmd", required=True, metavar="<command>")

    # describe
    p_desc = sub.add_parser(
        "describe",
        help="Print full command schema as JSON (one or all commands).")
    p_desc.add_argument("command", nargs="?", default=None,
                        help="Optional: limit output to one command")
    p_desc.set_defaults(func=cmd_describe)

    # load-file
    p_lf = sub.add_parser(
        "load-file",
        help="Validate a heap data file; report node/root counts. "
             "Auto-converts .loli files to .txt (cached on disk).")
    _add_file_arg(p_lf)
    _add_json_flag(p_lf)
    p_lf.set_defaults(func=cmd_load_file)

    # summary
    p_sum = sub.add_parser(
        "summary",
        help="Header stats, tree metadata, and the top 5 roots by abs(size).")
    _add_file_arg(p_sum)
    _add_json_flag(p_sum)
    p_sum.set_defaults(func=cmd_summary)

    # top
    p_top = sub.add_parser(
        "top",
        help="Top-N largest allocation nodes across all depths.")
    _add_file_arg(p_top)
    p_top.add_argument("-n", type=int, default=20,
                       help="Maximum results to return (default 20)")
    p_top.add_argument("--min-size-mb", type=float, default=0.0,
                       dest="min_size_mb",
                       help="Minimum absolute size in MB (default 0.0)")
    _add_json_flag(p_top)
    p_top.set_defaults(func=cmd_top)

    # children
    p_ch = sub.add_parser(
        "children",
        help="Direct children of a node, sorted by abs(size) descending.")
    _add_file_arg(p_ch)
    p_ch.add_argument("node_id", type=int)
    _add_json_flag(p_ch)
    p_ch.set_defaults(func=cmd_children)

    # call-path
    p_cp = sub.add_parser(
        "call-path",
        help="Trace the full call path from root to the target node.")
    _add_file_arg(p_cp)
    p_cp.add_argument("node_id", type=int)
    _add_json_flag(p_cp)
    p_cp.set_defaults(func=cmd_call_path)

    # search
    p_se = sub.add_parser(
        "search",
        help="Regex search across all function names.")
    _add_file_arg(p_se)
    p_se.add_argument("pattern",
                      help="Regular expression (case-insensitive)")
    p_se.add_argument("--max", type=int, default=30, dest="max",
                      help="Maximum results to return (default 30)")
    _add_json_flag(p_se)
    p_se.set_defaults(func=cmd_search)

    # subtree
    p_st = sub.add_parser(
        "subtree",
        help="Indented subtree view below a node, truncated at max-depth.")
    _add_file_arg(p_st)
    p_st.add_argument("node_id", type=int)
    p_st.add_argument("--max-depth", type=int, default=4, dest="max_depth",
                      help="Maximum depth to render (default 4)")
    _add_json_flag(p_st)
    p_st.set_defaults(func=cmd_subtree)

    return p


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        return args.func(args)
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    except RuntimeError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        import traceback
        traceback.print_exc(file=sys.stderr)
        print(f"Error: {type(e).__name__}: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
