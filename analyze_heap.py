#!/usr/bin/env python3
"""
Automated heap analysis using MCP server + Claude Code.

Instead of dumping the entire data file (potentially hundreds of thousands of
lines) into Claude's prompt, this script launches an MCP server that loads the
data and exposes query tools. Claude interactively explores the heap data,
searches source code, and writes a report — with only ~10-20KB of focused data
entering the context window.

Supports both file formats:
  - Diff files from `LoliProfilerCLI --compare` (two-profile comparison)
  - Snapshot files from `LoliProfilerCLI --dump` (single-profile export)

Usage:
    # Analyze a diff file
    python analyze_heap.py diff.txt --repo /path/to/source -o report.md

    # Analyze a snapshot file
    python analyze_heap.py snapshot.txt --repo /path/to/source -o report.md

    # HTML output
    python analyze_heap.py diff.txt --repo /path/to/source -o report.html

    # Different repos for baseline/comparison (diff only)
    python analyze_heap.py diff.txt --base-repo /v1 --target-repo /v2

    # Custom minimum size threshold
    python analyze_heap.py diff.txt --repo /path/to/source --min-size 1.0
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime

# Import markdown to HTML converter
try:
    from markdown_to_html import convert_file as convert_md_to_html
    HTML_CONVERSION_AVAILABLE = True
except ImportError:
    HTML_CONVERSION_AVAILABLE = False


def detect_file_mode(data_file: str) -> str:
    """Detect whether a heap data file is a diff or snapshot.

    Checks the first few lines for the report title to determine the format.
    Returns 'snapshot' or 'diff'.
    """
    with open(data_file, 'r', encoding='utf-8') as f:
        for line in f:
            if 'Profile Report' in line:
                return 'snapshot'
            if 'Comparison Report' in line:
                return 'diff'
            # Stop scanning after the first non-blank, non-=== line
            if line.strip() and not line.strip().startswith('==='):
                break
    return 'diff'  # default to diff for backward compatibility


def find_claude_command() -> str:
    """Find the claude CLI command."""
    for cmd in ('claude-internal', 'claude'):
        path = shutil.which(cmd)
        if path:
            return path
    return ""


def build_prompt(output_file: str,
                 base_repo: str,
                 target_repo: str,
                 min_size_mb: float,
                 mode: str = "diff") -> str:
    """Build the compact instruction prompt for Claude.

    This prompt contains NO data — it just tells Claude to use the MCP tools
    and source code search to perform the analysis.
    """
    same_repo = os.path.normpath(base_repo) == os.path.normpath(target_repo)

    if mode == "snapshot":
        # Snapshot mode: single-profile analysis
        repo_section = f"Source code repository: {base_repo}"

        return f"""Analyze a LoliProfiler heap snapshot using the loli-heap MCP tools, then cross-reference
source code to produce a detailed Chinese-language report.

{repo_section}

STEP-BY-STEP WORKFLOW:

1. Call get_summary() to understand overall stats (total allocations, total size).
2. Call get_top_allocations(20, {min_size_mb}) to find the biggest allocation hotspots.
3. For each significant hotspot:
   a. Call get_call_path(node_id) to see the full calling context.
   b. Call get_children(node_id) to see where memory branches below it.
   c. Use Grep/Read on the source code to find and understand the implementation.
   d. Determine WHAT data structures consume the most memory and WHY.
4. Optionally use search_function(pattern) to find specific modules or classes.
5. Write the complete analysis report.

IMPORTANT ANALYSIS GUIDELINES:
- Skip generic thread wrappers (FRunnableThreadPThread::Run, -[IOSAppDelegate MainAppThread:],
  FEngineLoop::Tick, etc.) — drill down to FUNCTIONAL-LEVEL functions.
- Focus on functions that describe specific work: rendering, audio, physics, AI, loading, etc.
- Only analyze allocations >= {min_size_mb} MB.
- This is READ-ONLY analysis. DO NOT modify any source code files.
- This is a SNAPSHOT (single profile), not a diff. Analyze memory DISTRIBUTION, not growth.

REPORT FORMAT (Chinese, code/function names in English):

# 内存快照分析报告

生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
分析工具: LoliProfiler
代码版本: {os.path.basename(base_repo)}

## 内存分布概况

[总体内存使用情况，各模块占比]

## 主要内存占用分析

### [1] [函数名] - [内存大小]

**完整调用栈:**
```
[从get_call_path获取的调用链]
```

**代码位置:** [file:line]

**源码分析:** [读取源码后说明这段代码做什么、分配什么数据结构]

**内存占用原因:** [结合源码分析解释为什么这里占用这么多内存]

**优化建议:** [具体可行的优化方向]

---

(repeat for each significant hotspot)

## 优化优先级建议

[按影响大小和实施难度排序]

## 总结

[整体内存分布分析和关键建议]

CRITICAL: Save the complete report to: {output_file}
Use the Write tool to save it. After saving, confirm with: "Report saved to: {output_file}"
"""
    else:
        # Diff mode: two-profile comparison (original behavior)
        if same_repo:
            repo_section = f"""Source code repository: {base_repo}
The two profiles are from different runs/builds of the same codebase."""
        else:
            repo_section = f"""Baseline source code: {base_repo}
Comparison source code: {target_repo}
The profiles are from different versions. You can diff files between repos."""

        return f"""Analyze a LoliProfiler heap diff using the loli-heap MCP tools, then cross-reference
source code to produce a detailed Chinese-language report.

{repo_section}

STEP-BY-STEP WORKFLOW:

1. Call get_summary() to understand overall stats.
2. Call get_top_allocations(20, {min_size_mb}) to find the biggest growth points.
3. For each significant hotspot:
   a. Call get_call_path(node_id) to see the full calling context.
   b. Call get_children(node_id) to see where memory branches below it.
   c. Use Grep/Read on the source code to find and understand the implementation.
   d. Determine WHY memory grows at this point and WHAT data structures are involved.
4. Optionally use search_function(pattern) to find specific modules or classes.
5. Write the complete analysis report.

IMPORTANT ANALYSIS GUIDELINES:
- Skip generic thread wrappers (FRunnableThreadPThread::Run, -[IOSAppDelegate MainAppThread:],
  FEngineLoop::Tick, etc.) — drill down to FUNCTIONAL-LEVEL functions.
- Focus on functions that describe specific work: rendering, audio, physics, AI, loading, etc.
- Only analyze allocations >= {min_size_mb} MB.
- This is READ-ONLY analysis. DO NOT modify any source code files.

REPORT FORMAT (Chinese, code/function names in English):

# 内存分析报告

生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
分析工具: LoliProfiler
基线版本: {os.path.basename(base_repo)}
对比版本: {os.path.basename(target_repo)}

## 主要内存增长点分析

### [1] [函数名] - [内存增长量]

**完整调用栈:**
```
[从get_call_path获取的调用链]
```

**代码位置:** [file:line]

**源码分析:** [读取源码后说明这段代码做什么、分配什么数据结构]

**增长原因:** [结合源码分析解释为什么产生这么多内存分配]

**优化建议:** [具体可行的优化方向]

---

(repeat for each significant hotspot)

## 优化优先级建议

[按影响大小和实施难度排序]

## 总结

[整体趋势分析和关键建议]

CRITICAL: Save the complete report to: {output_file}
Use the Write tool to save it. After saving, confirm with: "Report saved to: {output_file}"
"""


def build_mcp_config(data_file: str) -> dict:
    """Build the MCP server configuration."""
    server_script = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        'mcp_server', 'heap_explorer_server.py'
    )
    return {
        "mcpServers": {
            "loli-heap": {
                "command": sys.executable,
                "args": [server_script, "--file", os.path.abspath(data_file)],
                "env": {}
            }
        }
    }


def run_analysis(data_file: str,
                 base_repo: str,
                 target_repo: str,
                 output_file: str,
                 min_size_mb: float = 2.0,
                 timeout: int = 1800) -> bool:
    """Run Claude with MCP server to analyze heap data.

    Returns True on success, False otherwise.
    """
    claude_cmd = find_claude_command()
    if not claude_cmd:
        print("ERROR: 'claude-internal' or 'claude' command not found!", file=sys.stderr)
        print("Please ensure Claude CLI is installed and in your PATH.", file=sys.stderr)
        return False

    mode = detect_file_mode(data_file)
    print(f"Using Claude command: {claude_cmd}")
    print(f"Data file: {data_file} [mode: {mode}]")

    # Write temporary MCP config
    mcp_config = build_mcp_config(data_file)
    with tempfile.TemporaryDirectory(prefix='loli_mcp_') as tmp_dir:
        mcp_config_path = os.path.join(tmp_dir, '.mcp.json')
        with open(mcp_config_path, 'w') as f:
            json.dump(mcp_config, f, indent=2)

        print(f"MCP config: {mcp_config_path}")
        print(f"MCP server: {mcp_config['mcpServers']['loli-heap']['args']}")

        # Build prompt
        abs_output = os.path.abspath(output_file)
        prompt = build_prompt(abs_output, base_repo, target_repo, min_size_mb, mode=mode)

        prompt_size_kb = len(prompt.encode('utf-8')) / 1024
        print(f"Prompt size: {prompt_size_kb:.1f} KB (no data embedded — uses MCP tools)")
        print()

        try:
            if mode == "snapshot":
                cwd = base_repo if os.path.isdir(base_repo) else os.getcwd()
            else:
                cwd = target_repo if os.path.isdir(target_repo) else base_repo
            is_windows = sys.platform.startswith('win')

            # WARNING: --dangerously-skip-permissions allows Claude to execute
            # arbitrary tools without user confirmation. This is required for
            # batch/CI usage but should only be used in trusted environments.
            result = subprocess.run(
                [
                    claude_cmd, '-p',
                    '--verbose',
                    '--dangerously-skip-permissions',
                    '--mcp-config', mcp_config_path,
                ],
                input=prompt,
                capture_output=True,
                text=True,
                encoding='utf-8',
                errors='replace',
                timeout=timeout,
                cwd=cwd,
                shell=is_windows,
            )

            if result.returncode != 0:
                print(f"Claude analysis failed (exit {result.returncode}):", file=sys.stderr)
                if result.stderr:
                    print(result.stderr[:2000], file=sys.stderr)
                return False

            # Check if Claude wrote the report
            if os.path.exists(abs_output):
                with open(abs_output, 'r', encoding='utf-8') as f:
                    report = f.read()
                print("ANALYSIS COMPLETE")
                print(f"Report written by Claude to: {abs_output}")
            elif result.stdout.strip():
                # Fallback: save stdout
                report = result.stdout.strip()
                with open(abs_output, 'w', encoding='utf-8') as f:
                    f.write(report)
                print("ANALYSIS COMPLETE")
                print(f"Report written to: {abs_output}")
            else:
                print("Claude returned empty response", file=sys.stderr)
                return False

            # Preview
            print()
            print("Report preview:")
            print("-" * 80)
            lines = report.split('\n')
            for line in lines[:60]:
                print(line)
            if len(lines) > 60:
                print(f"\n... ({len(lines) - 60} more lines in full report)")
            print()

            return True

        except subprocess.TimeoutExpired:
            print(f"Analysis timed out after {timeout // 60} minutes", file=sys.stderr)
            return False
        except Exception as e:
            print(f"Error: {e}", file=sys.stderr)
            import traceback
            traceback.print_exc()
            return False


def main():
    parser = argparse.ArgumentParser(
        description='Analyze LoliProfiler heap data (diff or snapshot) via MCP server + Claude AI',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Analyze a diff with same repo for both versions
  python analyze_heap.py diff.txt --repo /path/to/game/source

  # Analyze a heap snapshot
  python analyze_heap.py snapshot.txt --repo /path/to/game/source

  # Analyze with different repos for baseline and comparison
  python analyze_heap.py diff.txt --base-repo /path/to/v1 --target-repo /path/to/v2

  # HTML output with custom threshold
  python analyze_heap.py diff.txt --repo /path/to/source --min-size 1.0 -o report.html

Compared to analyze_memory_diff.py, this version:
  - Sends ~2KB prompt instead of megabytes of data
  - Claude explores data interactively via MCP tools
  - Works with arbitrarily large files (tested with 588K+ nodes)
        """
    )

    parser.add_argument('data_file', help='Path to LoliProfilerCLI output file (diff from --compare, or snapshot from --dump)')
    parser.add_argument('--repo',
                        help='Path to source code repo (used for both baseline and comparison)')
    parser.add_argument('--base-repo',
                        help='Path to baseline version source code repo')
    parser.add_argument('--target-repo',
                        help='Path to comparison version source code repo')
    parser.add_argument('--output', '-o',
                        help='Output report file path (.md or .html, default: auto-generated .md)')
    parser.add_argument('--min-size', type=float, default=2.0,
                        help='Minimum allocation size threshold in MB (default: 2.0)')
    parser.add_argument('--timeout', '-t', type=int, default=1800,
                        help='Analysis timeout in seconds (default: 1800 = 30 minutes)')

    args = parser.parse_args()

    # Validate
    if not os.path.exists(args.data_file):
        print(f"Error: Data file not found: {args.data_file}", file=sys.stderr)
        return 1

    if args.repo:
        base_repo = target_repo = args.repo
    elif args.base_repo and args.target_repo:
        base_repo = args.base_repo
        target_repo = args.target_repo
    else:
        print("Error: Must specify either --repo or both --base-repo and --target-repo",
              file=sys.stderr)
        return 1

    if not os.path.exists(base_repo):
        print(f"Error: Base repo path not found: {base_repo}", file=sys.stderr)
        return 1
    if not os.path.exists(target_repo):
        print(f"Error: Target repo path not found: {target_repo}", file=sys.stderr)
        return 1

    # Output path
    output_file = args.output or f"memory_analysis_report_{datetime.now().strftime('%Y%m%d_%H%M%S')}.md"
    output_file = os.path.abspath(output_file)

    output_html = output_file.lower().endswith('.html')
    if output_html:
        md_output_file = output_file[:-5] + '.md'
    else:
        md_output_file = output_file

    # Run
    success = run_analysis(
        args.data_file,
        base_repo,
        target_repo,
        md_output_file,
        min_size_mb=args.min_size,
        timeout=args.timeout,
    )

    # HTML conversion
    if success and output_html:
        if HTML_CONVERSION_AVAILABLE:
            print("Converting markdown to HTML...")
            if convert_md_to_html(md_output_file, output_file):
                print(f"HTML report written to: {output_file}")
            else:
                print(f"Warning: HTML conversion failed, markdown at: {md_output_file}",
                      file=sys.stderr)
        else:
            print(f"Warning: HTML conversion requested but markdown_to_html.py not available",
                  file=sys.stderr)
            print(f"Markdown report available at: {md_output_file}", file=sys.stderr)

    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
