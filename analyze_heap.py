#!/usr/bin/env python3
"""
Automated heap analysis using the loli CLI + LLM CLI (CodeBuddy / Claude Code).

Instead of dumping the entire data file (potentially hundreds of thousands of
lines) into the LLM's prompt, this script teaches the LLM to call the `loli`
CLI (subcommands: summary, top, children, call-path, search, subtree) via the
Bash tool. The LLM interactively explores the heap data, searches source code,
and writes a report — with only ~10-20KB of focused data entering the context
window.

Supports these file formats:
  - .loli files (raw LoliProfiler capture, auto-converted by the CLI on first use)
  - .txt snapshot files from `LoliProfilerCLI --dump`
  - .txt diff files from `LoliProfilerCLI --compare`

Usage:
    # Analyze a raw .loli capture
    python analyze_heap.py snapshot.loli --no-source --model glm-5.1-ioa

    # Analyze a pre-converted .txt snapshot
    python analyze_heap.py snapshot.txt --repo /path/to/source -o report.md

    # HTML output with custom threshold
    python analyze_heap.py diff.txt --repo /path/to/source --min-size 1.0 -o report.html
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from collections import OrderedDict
from datetime import datetime

# Import markdown to HTML converter
try:
    from markdown_to_html import convert_file as convert_md_to_html
    HTML_CONVERSION_AVAILABLE = True
except ImportError:
    HTML_CONVERSION_AVAILABLE = False


def _format_duration(seconds: float) -> str:
    """Format seconds into a human-readable duration string."""
    if seconds < 60:
        return f"{seconds:.1f}s"
    minutes = int(seconds // 60)
    secs = seconds % 60
    if minutes < 60:
        return f"{minutes}m {secs:.0f}s"
    hours = minutes // 60
    mins = minutes % 60
    return f"{hours}h {mins}m {secs:.0f}s"


def detect_file_mode(data_file: str) -> str:
    """Detect whether a heap data file is a diff or snapshot.
    .loli files are always raw snapshots (diffs come from --compare as .txt).
    """
    if data_file.lower().endswith('.loli'):
        return 'snapshot'
    with open(data_file, 'r', encoding='utf-8') as f:
        for line in f:
            if 'Profile Report' in line:
                return 'snapshot'
            if 'Comparison Report' in line:
                return 'diff'
            if line.strip() and not line.strip().startswith('==='):
                break
    return 'diff'


def find_cli_command() -> str:
    """Find the CLI command to use for analysis.

    Search order:
      1. codebuddy / cbc  — supports many models (GPT, Gemini, Claude, etc.)
      2. claude-internal / claude — kept as fallback but not actively used
    """
    for cmd in ('codebuddy', 'cbc'):
        path = shutil.which(cmd)
        if path:
            return path
    # Fallback to claude-internal / claude — kept for environments without CodeBuddy
    # but not the primary path. These CLIs don't support --model.
    # for cmd in ('claude-internal', 'claude'):
    #     path = shutil.which(cmd)
    #     if path:
    #         return path
    return ""


def build_prompt(output_file: str,
                 base_repo: str,
                 target_repo: str,
                 min_size_mb: float,
                 mode: str = "diff",
                 data_file: str = "",
                 no_source: bool = False,
                 worklog_dir: str = "",
                 worklogs_enabled: bool = False) -> str:
    """Build the analysis prompt for the LLM.

    The prompt drives a 3-phase pipeline where the LLM does ALL the work:
      Phase 1+2: Walker sub-agent explores the heap tree via the loli CLI
      Phase 3:   Per-hotspot sub-agents do deep analysis, each writes result_<id>.md
      (Report assembly is done by the Python harness after the LLM exits)

    The LLM accesses heap data by shelling out to `python -m agentcli.cli ...`
    via the Bash tool. Each invocation is stateless — the file path is passed
    as the first positional argument every time.

    This lets the LLM reason about the data like a human engineer: adapting
    its walk depth, noticing patterns, and making judgment calls.
    """
    # Normalize repos when running without source
    if no_source:
        base_repo = base_repo or ""
        target_repo = target_repo or base_repo

    same_repo = os.path.normpath(base_repo or "") == os.path.normpath(target_repo or "")

    # --- Mode-specific bits ---
    if mode == "snapshot":
        report_title = "内存快照分析报告"
        base_label = os.path.basename(base_repo) if base_repo else "N/A (no source)"
        header_rows = f"| 代码版本 | {base_label} |"
        if no_source:
            source_section = (
                "No source code repository is available. Do NOT call Grep or Read on any "
                "source tree. Reason about each hotspot using generic knowledge: Unreal "
                "Engine 4 internals, common third-party libraries, and standard C/C++ "
                "runtime patterns."
            )
        else:
            source_section = (
                f"Source code repository: {base_repo}" if base_repo
                else "No source code repository provided."
            )
    else:
        report_title = "内存对比分析报告"
        base_label = os.path.basename(base_repo) if base_repo else "N/A (no source)"
        target_label = os.path.basename(target_repo) if target_repo else "N/A (no source)"
        header_rows = (
            f"| 基线版本 | {base_label} |\n"
            f"| 对比版本 | {target_label} |"
        )
        if no_source:
            source_section = (
                "No source code repository is available for either profile. Do NOT call "
                "Grep or Read on any source tree. Reason about each hotspot using generic "
                "knowledge: Unreal Engine 4 internals, common third-party libraries, and "
                "standard C/C++ runtime patterns."
            )
        elif same_repo:
            source_section = f"""Source code repository: {base_repo}
The two profiles are from different runs/builds of the same codebase."""
        else:
            source_section = f"""Baseline source code: {base_repo}
Comparison source code: {target_repo}
The profiles are from different versions. You can diff files between repos."""
        source_repo = target_repo if target_repo and os.path.isdir(target_repo) else base_repo

    if no_source:
        source_grep_instruction = (
            "No source repo available — DO NOT use Grep or Read to search source code. "
            "Skip step 2 and go directly to step 3 (composition via the `children` CLI). "
            "Reason about the hotspot using general knowledge of Unreal Engine 4, "
            "the Unreal rendering pipeline, common third-party libraries, and "
            "standard C/C++ runtime."
        )
    elif base_repo:
        source_grep_instruction = (
            f"Source code repo available at {source_repo}. GREP FIRST, then read:\n"
            "   a. Use Grep to locate the function (pattern like 'ClassName::MethodName' "
            "      or the bare method signature).\n"
            "   b. Use Read on the most promising file(s) to understand what data "
            "      structures are allocated, buffer sizes, container growth, caching "
            "      behavior.\n"
            "   c. Record the file:line in CODE_LOCATION of your return."
        )
    else:
        source_grep_instruction = (
            "No source repo configured — skip source code analysis. Go directly "
            "to step 3 (composition via the `children` CLI)."
        )

    # ── Build conditional prompt fragments for worklogs ────────────────
    if worklogs_enabled:
        worklog_description = (
            f"WORKLOG DIRECTORY (already created): {worklog_dir}\n"
            "When --worklogs is enabled, sub-agents write debug trace files here."
        )
        walker_worklog_instruction = (
            f"WORKLOG — FIRST ACTION:\n"
            f"Use the Write tool to create: {worklog_dir}/phase12_walker.md\n"
            "Initial content:\n"
            "    # Phase 1+2 worklog — walker\n"
            "    ## Tool Calls\n"
            "    ## Result\n"
            "Append one bullet under \"## Tool Calls\" after every tool call you make.\n"
            "Append THREADS_AND_HOTSPOTS to \"## Result\" before returning."
        )
        phase3_worklog_instruction = (
            "    WORKLOG — WRITE THIS BEFORE OTHER TOOL CALLS:\n"
            "    Use the Write tool to create:\n"
            f"        {worklog_dir}/phase3_[node_id]_[sanitized_function_name].md\n"
            "    (sanitize: replace non-[A-Za-z0-9_.-] with \"_\", max 60 chars)\n"
            "    Initial content:\n"
            "        # Phase 3 worklog — [function_name] (node_id=[node_id], size=[X.X] MB)\n"
            "        ## Tool Calls\n"
            "        ## Final output\n"
            "    Append one bullet under \"## Tool Calls\" after each tool call.\n"
        )
    else:
        worklog_description = ""
        walker_worklog_instruction = ""
        phase3_worklog_instruction = ""

    # ── Tool-invocation instructions (loli CLI via Bash) ─────────────────
    abs_data_file = os.path.abspath(data_file)
    # The CLI is invoked via Bash. Each call is stateless: every command
    # takes the data file path as the first positional argument, which we
    # pre-fill below.
    #
    # Only the three subcommands the walker / Phase-3 sub-agents actually
    # need are advertised here: `summary`, `children`, `call-path`. The CLI
    # supports more (top, search, subtree, load-file) but they're not used
    # by this workflow — listing them only wastes prompt tokens and tempts
    # the LLM into unnecessary calls.
    tool_reference = f"""HEAP DATA TOOLS:
The heap data is queried via the `loli` CLI, run through the Bash tool. Each
invocation is stateless — pass the file path as the first positional argument.

The data file is:
    {abs_data_file}

Available subcommands (the only three you need):

  python -m agentcli.cli summary <FILE>
      Header stats + tree metadata + top 5 roots. Run this once at the start
      to get total size, total allocations, and root count.

  python -m agentcli.cli children <FILE> <NODE_ID>
      Direct children of a node, sorted by size descending. Each line:
          [<child_node_id>] <size>, count=<N> | <function_name>
      This is your primary tool for descending the call tree.

  python -m agentcli.cli call-path <FILE> <NODE_ID>
      Trace the full call path from root down to <NODE_ID>. Use this once
      per hotspot to capture the complete calling context.

Usage examples (run via Bash):
    python -m agentcli.cli summary "{abs_data_file}"
    python -m agentcli.cli children "{abs_data_file}" 12345
    python -m agentcli.cli call-path "{abs_data_file}" 12345

NOTE: the very first call against a `.loli` file may take 10-30s while it
auto-converts to `.txt` (cached on disk for subsequent calls). All later
calls are fast.
"""
    # Pre-baked tool-call snippets used inside the walker / phase-3 briefs.
    walker_summary_step = f'python -m agentcli.cli summary "{abs_data_file}"     (via Bash)'
    walker_get_children_call = f'python -m agentcli.cli children "{abs_data_file}" <node_id>'
    phase3_call_path_step = (
        f'Run via Bash:\n       python -m agentcli.cli call-path '
        f'"{abs_data_file}" [node_id]'
    )
    phase3_children_step = (
        f'Run via Bash:\n       python -m agentcli.cli children '
        f'"{abs_data_file}" [node_id]\n'
        f'    to see the COMPOSITION — what specifically accounts for the [X.X] MB?\n'
        f'    If any child is >= 30% of the hotspot size, run children on that child too\n'
        f'    (one level deeper).'
    )

    return f"""You are a memory analysis agent. Analyze a LoliProfiler {report_title} using
the loli CLI heap-data tools. Follow the phases below EXACTLY in order.

{source_section}

{worklog_description}

RESULT DIRECTORY (already created): {worklog_dir}
Phase 3 sub-agents MUST write their analysis to result_<node_id>.md files here.
The Python harness will assemble these into the final report after you finish.

{tool_reference}

═══════════════════════════════════════════════════════════════
PHASE 1+2: DATA LOADING AND THREAD-GROUPED HOTSPOT DISCOVERY (walker sub-agent)
═══════════════════════════════════════════════════════════════

Dispatch ONE sub-agent (the "walker") via a single Agent tool-use block. The walker
loads the heap data, walks the call tree, discovers thread-grouped hotspots, and
returns structured data. You do NOT call any heap-data tools yourself during this phase.

Copy everything between the ── WALKER BRIEF ── markers below as the Agent prompt.
All values are pre-filled — paste verbatim, do not modify any part:

── WALKER BRIEF START ──────────────────────────────────────────
You are a heap-profile tree walker. Load a LoliProfiler data file, walk the call
tree from roots down, and return thread-grouped memory hotspots.

{walker_worklog_instruction}

Steps:

1. {walker_summary_step}
   — record total size, total allocations, root count.
   (This first call also implicitly loads/parses the heap data file.)

The walk algorithm is STRUCTURAL — no hard-coded function names. Two primitives:

  A node is a PASS-THROUGH when it has exactly one child whose size is >= {min_size_mb}
  MB and that child's size is >= 90% of the node's own size.

  A node is a FAN-OUT when it has two or more children with size >= {min_size_mb} MB.

KNOWN THREAD PATTERNS (hints — these are NOT hard-coded filters):
  These function names commonly appear as thread entry points or intermediate wrappers
  in UE4 iOS profiles. If you encounter them during the walk, they are likely
  pass-through wrappers that lead to deeper hotspots. Do NOT record them as hotspots
  or thread labels themselves — keep descending through them.

  System / pthread layer (always pass-through):
    thread_start, _pthread_start, start_wqthread, _pthread_wqthread,
    start, main, UIApplicationMain, -[UIApplication _run], GSEventRunModal,
    _CFRunLoopRunSpecificWithOptions, __CFRunLoopRun, libsystem_pthread!*

  UE thread dispatch (pass-through to actual thread entry):
    FRunnableThreadPThread::_ThreadProc, FRunnableThreadPThread::Run,
    __NSThread__start__, -[IOSAppDelegate MainAppThread:], Foundation!*

  UE engine threads (use these as thread LABELS when the walk reaches them):
    FAsyncLoadingThread::Run — asset loading thread
    FRenderingThread::Run — rendering command thread
    FTaskThreadAnyThread::Run — worker pool thread
    FQueuedThread::Run — queued thread pool
    FStatsThread::Run — stats collection
    FChunkCacheWorker::Run — chunk cache

  Game thread (use "GameThread" as label):
    FAppEntry::Tick, FAppEntry::Init, FEngineLoop::Tick,
    FEngineLoop::PreInitPostStartupScreen

  Audio threads:
    CAkThreadedBankMgr::BankThreadFunc — Wwise bank loading
    CAkAudioThread::EventMgrThreadFunc — Wwise audio events

  iOS dispatch / GCD (use the deepest meaningful function as label):
    __CFRUNLOOP_IS_SERVICING_THE_MAIN_DISPATCH_QUEUE__,
    _dispatch_main_queue_callback_4CF, _dispatch_main_queue_drain,
    _dispatch_client_callout, _dispatch_call_block_and_release,
    _dispatch_workloop_worker_thread, _dispatch_worker_thread2

  These hints help you avoid recording "FRunnableThreadPThread::_ThreadProc" as a
  thread when the real threads (FAsyncLoadingThread, FRenderingThread, etc.) are
  one level deeper. The structural walk (pass-through/fan-out) is still the PRIMARY
  logic — these hints supplement it.

2. UNIFIED WALK — discovers threads AND hotspots in one pass.

   The walk descends from each root through pass-through chains. The FIRST fan-out
   from a root defines the thread labels. Subsequent fan-outs inside a thread
   produce hotspots. There are no separate "thread discovery" and "hotspot discovery"
   steps — they are the same algorithm, and MUST be executed in a single continuous
   walk. Do NOT stop at thread boundaries.

   Maintain a global hotspot list: [(thread_label, node_id, name, size_mb), ...]

   def walk(node, thread_label):
     # Terminal-allocator shortcut: if node's function name matches
     #   malloc, _malloc*, realloc, _realloc*, operator new, operator delete,
     #   *::Malloc, *::Realloc, *::Free, *::ResizeTo, *::ResizeGrow, FMemory::*,
     #   FMalloc*::*, mmap, *_zone_malloc*, *_zone_realloc*
     # then do NOT call children — return immediately.
     if node.function_name matches a terminal-allocator pattern:
       return

     children = run({walker_get_children_call})
     BIG = children with size >= {min_size_mb} MB

     if len(BIG) == 0:
       label = thread_label or node.function_name
       record(label, node)
       return

     if len(BIG) == 1 and BIG[0].size >= 0.9 * node.size:
       # Pass-through: one dominant child. Keep descending.
       walk(BIG[0], thread_label)
       return

     if len(BIG) == 1 and BIG[0].size < 0.9 * node.size:
       # Node has meaningful own-size alongside one big child.
       label = thread_label or node.function_name
       record(label, node)
       walk(BIG[0], label)
       return

     # len(BIG) >= 2  →  FAN-OUT.
     if thread_label is None:
       # FIRST fan-out from root. Each BIG child defines a thread.
       for c in BIG:
         walk(c, thread_label=c.function_name)
     else:
       # Fan-out inside a thread → sub-hotspots.
       own = node.size - sum(c.size for c in BIG)
       if own >= {min_size_mb}: record(thread_label, node)
       for c in BIG:
         walk(c, thread_label)

   For each root whose size >= {min_size_mb} MB: call walk(root, thread_label=None).

   CRITICAL: the walk MUST continue past thread boundaries into the actual hotspots.

   Notes:
     - DEDUP: if an existing hotspot is an ancestor/descendant of a new one,
       keep only the deeper (more specific) node.
     - Cap total recorded hotspots at 25.

3. Return EXACTLY this format (no other text before or after):

SUMMARY_STATS:
total_size: [from summary, e.g. "1.76 GB"]
total_allocs: [from summary, e.g. "489,493"]
root_count: [root count]
thread_count: [number of unique thread labels]

THREADS_AND_HOTSPOTS:
  Thread: <thread_label> (<sum of hotspot sizes> MB)
    - [<node_id>] <function_name>  <size_mb> MB
    ...
  Thread: <next_thread> (<sum> MB)
    ...

Sort threads by summed hotspot size descending; hotspots within thread by size descending.

SELF-CHECK: if ANY thread has only ONE hotspot whose size equals the entire thread,
the walk stopped too early. Run `children` on that node and continue descending.
── WALKER BRIEF END ────────────────────────────────────────────

After the walker returns, extract SUMMARY_STATS and THREADS_AND_HOTSPOTS from its
response. Keep both in context — you will use them to drive Phase 3 dispatch.

Also: use the Write tool to save the walker's output to:
    {worklog_dir}/phase12_result.txt
This lets the Python harness read it for report assembly.


═══════════════════════════════════════════════════════════════
PHASE 3: PER-HOTSPOT DEEP ANALYSIS (sub-agents writing result files)
═══════════════════════════════════════════════════════════════

Each hotspot from the walker's THREADS_AND_HOTSPOTS gets one deep-analysis sub-agent.
Each sub-agent writes a self-contained markdown section to a result file.

DISPATCH RULE:
  For each hotspot, dispatch ONE Agent tool-use block. Try to group multiple Agent
  dispatches in a SINGLE assistant message for parallelism.

Each **hotspot sub-agent** receives this brief (fill in the bracketed values):

    You are performing deep analysis on a memory hotspot in a LoliProfiler heap profile.
    Function: [function_name]
    Node ID: [node_id]
    Size: [X.X] MB
    Thread context: [thread_name]

{phase3_worklog_instruction}
    Exploration steps:
    1. {phase3_call_path_step}
       to confirm the full calling context.
    2. {source_grep_instruction}
    3. {phase3_children_step}

    RESULT FILE — MANDATORY:
    Use the Write tool to save your analysis to:
        {worklog_dir}/result_[node_id].md

    The file MUST have this exact format (YAML frontmatter + markdown body):

    ---
    function: [full function name]
    node_id: [id]
    size_mb: [X.X]
    thread: [thread_name]
    ---

    ### [function_name] — [X.X] MB

    **完整调用栈:**
    ```
    FunctionRoot (XX.X MB)
    └── ChildFunction (XX.X MB)
        └── ... down to the hotspot function
    ```

    **代码位置:** [file:line or N/A]

    **源码分析:** [2-3 sentences in Chinese: what this code does, what it allocates]

    **内存占用原因:** [2-3 sentences in Chinese: why this allocation is large,
    reference specific children from the children query]

    **优化建议:** [2-3 sentences in Chinese: specific, actionable optimization suggestions]

    ---

    After writing the result file, return a brief confirmation:
    "Result written: result_[node_id].md"

After ALL Phase 3 sub-agents have returned, confirm they all wrote their result files.
Then return: "All phases complete. Result files in {worklog_dir}"

The Python harness will assemble result files into the final report — you do NOT need
to write the report yourself. Your job is done after Phase 3.

IMPORTANT RULES:
- Only analyze allocations >= {min_size_mb} MB.
- This is READ-ONLY analysis. Do NOT modify any source code files.
- All prose in Chinese. All code/function names in English.
- PHASE 1+2: dispatch ONE walker sub-agent. Copy the brief VERBATIM.
- PHASE 3: dispatch one Agent per hotspot. Each writes result_<id>.md.
- Do NOT write the final report — the harness does that from result files.
"""


def _worklog_dir_for(output_file: str) -> str:
    """Compute the sibling worklog directory for a given report output path."""
    base = output_file
    for ext in ('.md', '.html', '.htm'):
        if base.lower().endswith(ext):
            base = base[: -len(ext)]
            break
    return os.path.abspath(base + '.worklogs')


# ── Report assembly (runs after the LLM exits) ────────────────────────

def _assemble_report(worklog_dir: str, output_file: str,
                     report_title: str, header_rows: str,
                     gen_time: str, start_time: str = "",
                     duration_str: str = "",
                     cli_name: str = "", model_name: str = "") -> bool:
    """Read result files from worklog_dir and assemble the final report.

    Each Phase 3 agent writes result_<node_id>.md with YAML frontmatter
    (function, node_id, size_mb, thread) and a markdown body. The walker
    writes phase12_result.txt with SUMMARY_STATS and THREADS_AND_HOTSPOTS.

    Returns True if a report was assembled, False if no result files found.
    """
    # Read walker output
    walker_path = os.path.join(worklog_dir, 'phase12_result.txt')
    summary_stats = {}
    if os.path.exists(walker_path):
        with open(walker_path, 'r', encoding='utf-8', errors='replace') as f:
            walker_text = f.read()
        # Parse SUMMARY_STATS
        for line in walker_text.split('\n'):
            if ':' in line and not line.startswith('SUMMARY') and not line.startswith('THREAD'):
                key, _, val = line.partition(':')
                summary_stats[key.strip()] = val.strip()

    # Read all result files
    results = []
    try:
        for fname in os.listdir(worklog_dir):
            if fname.startswith('result_') and fname.endswith('.md'):
                fpath = os.path.join(worklog_dir, fname)
                with open(fpath, 'r', encoding='utf-8', errors='replace') as f:
                    content = f.read()
                # Parse YAML frontmatter
                meta = {}
                body = content
                fm_match = re.match(r'^---\s*\n(.*?)\n---\s*\n(.*)$', content, re.DOTALL)
                if fm_match:
                    for line in fm_match.group(1).split('\n'):
                        if ':' in line:
                            k, _, v = line.partition(':')
                            meta[k.strip()] = v.strip()
                    body = fm_match.group(2)
                results.append({
                    'meta': meta,
                    'body': body.strip(),
                    'file': fname,
                })
    except OSError:
        pass

    if not results:
        return False

    # Group by thread
    threads = OrderedDict()
    for r in results:
        thread = r['meta'].get('thread', 'Unknown')
        threads.setdefault(thread, []).append(r)

    # Sort threads by total size descending
    def _thread_size(items):
        return sum(float(r['meta'].get('size_mb', 0)) for r in items)

    sorted_threads = sorted(threads.items(), key=lambda t: _thread_size(t[1]), reverse=True)

    # Sort hotspots within each thread by size descending
    for thread_name, items in sorted_threads:
        items.sort(key=lambda r: float(r['meta'].get('size_mb', 0)), reverse=True)

    # Build report
    lines = []
    lines.append(f"# {report_title}\n")
    lines.append("| 项目 | 值 |")
    lines.append("|------|-----|")
    if start_time:
        lines.append(f"| 开始时间 | {start_time} |")
    lines.append(f"| 生成时间 | {gen_time} |")
    if duration_str:
        lines.append(f"| 分析耗时 | {duration_str} |")
    tool_label = "LoliProfiler + "
    if cli_name and model_name:
        tool_label += f"{cli_name} ({model_name})"
    elif cli_name:
        tool_label += cli_name
    else:
        tool_label += "LLM CLI"
    lines.append(f"| 分析工具 | {tool_label} |")
    lines.append(header_rows)
    lines.append("")

    # Compute totals up front
    total_size = sum(_thread_size(items) for _, items in sorted_threads)
    hotspot_num = sum(len(items) for _, items in sorted_threads)

    # Summary stats (includes thread/hotspot counts — no separate 分析统计 section)
    lines.append("## 内存分布概况\n")
    lines.append("| 指标 | 值 |")
    lines.append("|------|-----|")
    lines.append(f"| 总内存 | {summary_stats.get('total_size', 'N/A')} |")
    lines.append(f"| 总分配次数 | {summary_stats.get('total_allocs', 'N/A')} |")
    lines.append(f"| 根节点数 | {summary_stats.get('root_count', 'N/A')} |")
    lines.append(f"| 识别线程数 | {len(sorted_threads)} |")
    lines.append(f"| 分析热点数 | {hotspot_num} |")
    lines.append("")

    # Thread distribution table
    lines.append("## 线程内存分布\n")
    lines.append("| 线程 | 内存占用 | 占比 |")
    lines.append("|------|---------|------|")
    for thread_name, items in sorted_threads:
        ts = _thread_size(items)
        pct = (ts / total_size * 100) if total_size > 0 else 0
        lines.append(f"| {thread_name} | {ts:.2f} MB | {pct:.1f}% |")
    lines.append("")

    # Per-thread hotspot sections
    for thread_name, items in sorted_threads:
        ts = _thread_size(items)
        lines.append(f"## {thread_name} ({ts:.2f} MB)\n")
        for r in items:
            lines.append(r['body'])
            lines.append("")  # blank line between hotspots

    report = "\n".join(lines)
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(report)
    return True


# ── Session diagnostics ───────────────────────────────────────────────

def _find_session_jsonl(cwd: str, started_after: float) -> str | None:
    """Locate the session transcript for a run we just launched."""
    home = os.path.expanduser('~')
    # Try both .claude-internal and .codebuddy project dirs
    for proj_root_name in ('.claude-internal', '.codebuddy'):
        projects_root = os.path.join(home, proj_root_name, 'projects')
        if not os.path.isdir(projects_root):
            continue

        abs_cwd = os.path.abspath(cwd)
        drive, rest = os.path.splitdrive(abs_cwd)
        drive = drive.rstrip(':')
        slug = (drive + rest).replace('\\', '-').replace('/', '-').strip('-')

        project_dir = os.path.join(projects_root, slug)
        if os.path.isdir(project_dir):
            candidates = []
            for f in os.listdir(project_dir):
                if f.endswith('.jsonl'):
                    p = os.path.join(project_dir, f)
                    try:
                        mt = os.path.getmtime(p)
                    except OSError:
                        continue
                    if mt >= started_after:
                        candidates.append((mt, p))
            if candidates:
                candidates.sort(reverse=True)
                return candidates[0][1]
    return None


def _inspect_session(jsonl_path: str) -> dict:
    """Extract diagnostics from a session transcript.

    Supports both Claude Code and CodeBuddy JSONL formats:
      - Claude: type="assistant", message.model, message.content[].type="tool_use"
      - CodeBuddy: type="message" role="assistant", providerData.model,
                   type="function_call", type="function_call_result"
    """
    model = None
    tool_calls = 0
    last_tool = None
    last_assistant_text = ''
    stopped_with = None
    last_error = ''

    try:
        with open(jsonl_path, 'r', encoding='utf-8', errors='replace') as f:
            for line in f:
                try:
                    j = json.loads(line)
                except json.JSONDecodeError:
                    continue
                t = j.get('type')

                # Claude Code format
                if t == 'assistant':
                    msg = j.get('message', {}) or {}
                    if not model:
                        m = msg.get('model')
                        if m:
                            model = m
                    for c in msg.get('content', []) or []:
                        if c.get('type') == 'tool_use':
                            tool_calls += 1
                            last_tool = (c.get('name', ''),
                                         json.dumps(c.get('input', {}), ensure_ascii=False))
                        elif c.get('type') == 'text':
                            txt = c.get('text', '').strip()
                            if txt:
                                last_assistant_text = txt

                # CodeBuddy format: messages with role
                elif t == 'message':
                    role = j.get('role')
                    pd = j.get('providerData', {}) or {}
                    if not model:
                        m = pd.get('model')
                        if m:
                            model = m
                    if role == 'assistant':
                        for c in j.get('content', []) or []:
                            txt = c.get('text', '').strip()
                            if txt:
                                last_assistant_text = txt
                        # Check for error in providerData
                        err = pd.get('error', {})
                        if err:
                            last_error = err.get('message', str(err))

                # CodeBuddy format: function calls
                elif t == 'function_call':
                    tool_calls += 1
                    name = j.get('name', '')
                    args = j.get('arguments', '')
                    if isinstance(args, str) and len(args) > 200:
                        args = args[:200] + '...'
                    last_tool = (name, args)

                # CodeBuddy format: function call results (may contain errors)
                elif t == 'function_call_result':
                    output = j.get('output', {})
                    if isinstance(output, dict):
                        text = output.get('text', '')
                    else:
                        text = str(output)
                    if 'error' in text.lower() or '429' in text:
                        last_error = text[:500]

                elif t == 'attachment':
                    att = j.get('attachment', {}) or {}
                    if att.get('hookEvent') == 'Stop':
                        stopped_with = 'Stop'
    except OSError:
        pass

    return {
        'model': model,
        'tool_calls': tool_calls,
        'last_tool': last_tool,
        'last_assistant_text': last_assistant_text,
        'stopped_with': stopped_with,
        'last_error': last_error,
    }


def _format_diagnostics(diag: dict, jsonl_path: str | None) -> str:
    """Human-readable diagnostics string for stderr on failure."""
    lines = ['=' * 70, 'DIAGNOSTICS', '=' * 70]
    lines.append(f"Model:               {diag.get('model') or '(unknown)'}")
    lines.append(f"Total tool calls:    {diag.get('tool_calls')}")
    lines.append(f"Stopped via hook:    {diag.get('stopped_with') or '(no)'}")
    err = diag.get('last_error') or ''
    if err:
        lines.append(f"Last error:          {err[:300]}")
    lt = diag.get('last_tool')
    if lt:
        name, args = lt
        lines.append(f"Last tool call:      {name}({args[:200]})")
    text = diag.get('last_assistant_text') or ''
    if text:
        preview = text[:500].replace('\r', ' ').replace('\n', ' <- ')
        lines.append(f"Last assistant text: {preview}")
    if jsonl_path:
        lines.append(f"Full transcript:     {jsonl_path}")
    lines.append('=' * 70)
    return '\n'.join(lines)


# ── Main analysis runner ──────────────────────────────────────────────

def run_analysis(data_file: str,
                 base_repo: str,
                 target_repo: str,
                 output_file: str,
                 min_size_mb: float = 10.0,
                 timeout: int = 1800,
                 no_source: bool = False,
                 worklogs_enabled: bool = False,
                 model: str = "") -> bool:
    """Run an LLM CLI to analyze heap data.

    The LLM does Phase 1+2 (walk) and Phase 3 (deep analysis), driving the
    `loli` CLI through the Bash tool (`python -m agentcli.cli ...`) for all
    heap-data queries. The Python harness then assembles the per-hotspot
    result files into the final report.
    """
    cli_cmd = find_cli_command()
    if not cli_cmd:
        print("ERROR: No supported CLI found (codebuddy, cbc)!", file=sys.stderr)
        print("Install CodeBuddy: npm i -g @anthropic-ai/codebuddy", file=sys.stderr)
        return False

    mode = detect_file_mode(data_file)
    print(f"Using CLI: {cli_cmd}")
    if model:
        print(f"Model: {model}")
    print(f"Data file: {data_file} [mode: {mode}]")

    abs_output = os.path.abspath(output_file)
    worklog_dir = _worklog_dir_for(abs_output)
    os.makedirs(worklog_dir, exist_ok=True)
    print(f"Worklog dir: {worklog_dir}")

    # The directory containing this script — parent of the agentcli/ package.
    # We add it to PYTHONPATH for the LLM subprocess so child Bash invocations
    # of `python -m agentcli.cli ...` resolve regardless of cwd.
    repo_root = os.path.dirname(os.path.abspath(__file__))

    prompt = build_prompt(abs_output, base_repo, target_repo, min_size_mb,
                          mode=mode, data_file=data_file, no_source=no_source,
                          worklog_dir=worklog_dir,
                          worklogs_enabled=worklogs_enabled)

    prompt_size_kb = len(prompt.encode('utf-8')) / 1024
    print(f"Prompt size: {prompt_size_kb:.1f} KB")
    print()

    try:
        cwd = os.getcwd()
        if mode == "snapshot":
            if base_repo and os.path.isdir(base_repo):
                cwd = base_repo
        else:
            if target_repo and os.path.isdir(target_repo):
                cwd = target_repo
            elif base_repo and os.path.isdir(base_repo):
                cwd = base_repo
        is_windows = sys.platform.startswith('win')

        cmd_args = [
            cli_cmd, '-p',
            '--verbose',
            '--dangerously-skip-permissions',
        ]
        if model:
            cmd_args.extend(['--model', model])

        # Inject PYTHONPATH so child `python -m agentcli.cli ...` calls
        # can find the agentcli package no matter what cwd the LLM ran
        # under. Inherit the rest of the environment.
        child_env = os.environ.copy()
        existing_pp = child_env.get("PYTHONPATH", "")
        sep = os.pathsep
        if repo_root and repo_root not in existing_pp.split(sep):
            child_env["PYTHONPATH"] = (
                f"{repo_root}{sep}{existing_pp}" if existing_pp else repo_root
            )

        t_start = time.monotonic()
        started_wall = time.time()
        result = subprocess.run(
            cmd_args,
            input=prompt,
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='replace',
            timeout=timeout,
            cwd=cwd,
            shell=is_windows,
            env=child_env,
        )
        duration_sec = time.monotonic() - t_start

        session_jsonl = _find_session_jsonl(cwd, started_wall - 5)
        diag = _inspect_session(session_jsonl) if session_jsonl else {
            'model': None, 'tool_calls': 0, 'last_tool': None,
            'last_assistant_text': '', 'stopped_with': None,
        }

        if result.returncode != 0:
            print(f"Analysis failed (exit {result.returncode}):", file=sys.stderr)
            if result.stderr:
                print(result.stderr[:2000], file=sys.stderr)
            print(_format_diagnostics(diag, session_jsonl), file=sys.stderr)
            # Still try to assemble whatever result files were produced
            print("Attempting partial report assembly from result files...",
                  file=sys.stderr)

        print(f"\nLLM phase duration: {_format_duration(duration_sec)}")

        # List result files
        try:
            result_files = [f for f in os.listdir(worklog_dir)
                            if f.startswith('result_') and f.endswith('.md')]
            worklog_files = [f for f in os.listdir(worklog_dir)
                             if f.endswith('.md') and not f.startswith('result_')]
        except OSError:
            result_files = []
            worklog_files = []
        print(f"Result files: {len(result_files)}")
        if worklog_files:
            print(f"Debug worklogs: {len(worklog_files)} files")

        # Assemble report from result files
        if mode == "snapshot":
            report_title = "内存快照分析报告"
            base_label = os.path.basename(base_repo) if base_repo else "N/A (no source)"
            header_rows = f"| 代码版本 | {base_label} |"
        else:
            report_title = "内存对比分析报告"
            base_label = os.path.basename(base_repo) if base_repo else "N/A (no source)"
            target_label = os.path.basename(target_repo) if target_repo else "N/A (no source)"
            header_rows = (
                f"| 基线版本 | {base_label} |\n"
                f"| 对比版本 | {target_label} |"
            )

        gen_time = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        start_time_str = datetime.fromtimestamp(started_wall).strftime('%Y-%m-%d %H:%M:%S')
        cli_basename = os.path.splitext(os.path.basename(cli_cmd))[0]
        if _assemble_report(worklog_dir, abs_output, report_title,
                            header_rows, gen_time,
                            start_time=start_time_str,
                            duration_str=_format_duration(duration_sec),
                            cli_name=cli_basename,
                            model_name=model):
            with open(abs_output, 'r', encoding='utf-8') as f:
                report = f.read()
            print(f"\nANALYSIS COMPLETE")
            print(f"Report assembled from {len(result_files)} result files: {abs_output}")
        elif os.path.exists(abs_output):
            # LLM may have written the report directly (legacy behavior)
            with open(abs_output, 'r', encoding='utf-8') as f:
                report = f.read()
            print(f"\nANALYSIS COMPLETE")
            print(f"Report written by LLM: {abs_output}")
        else:
            print("No result files and no report produced.", file=sys.stderr)
            print(_format_diagnostics(diag, session_jsonl), file=sys.stderr)
            return False

        # Preview
        print()
        print("Report preview:")
        print("-" * 80)
        for line in report.split('\n')[:60]:
            print(line)
        if len(report.split('\n')) > 60:
            print(f"\n... ({len(report.split(chr(10))) - 60} more lines)")
        print()

        return True

    except subprocess.TimeoutExpired:
        print(f"Analysis timed out after {timeout // 60} minutes", file=sys.stderr)
        # Try partial assembly
        gen_time = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        if mode == "snapshot":
            rt, hr = "内存快照分析报告", f"| 代码版本 | N/A |"
        else:
            rt, hr = "内存对比分析报告", "| 基线版本 | N/A |\n| 对比版本 | N/A |"
        if _assemble_report(worklog_dir, abs_output, rt, hr, gen_time,
                            cli_name=os.path.splitext(os.path.basename(cli_cmd))[0],
                            model_name=model):
            print(f"Partial report assembled from available result files: {abs_output}")
        return False
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return False


def main():
    parser = argparse.ArgumentParser(
        description='Analyze LoliProfiler heap data via the loli CLI + LLM CLI',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python analyze_heap.py snapshot.loli --no-source --model glm-5.1-ioa
  python analyze_heap.py snapshot.txt --repo /path/to/source
  python analyze_heap.py snapshot.txt --no-source --model gpt-5.5
  python analyze_heap.py diff.txt --repo /path/to/source --min-size 1.0 -o report.html
        """
    )

    parser.add_argument('data_file',
                        help='Path to heap data file (.loli or .txt from LoliProfilerCLI)')
    parser.add_argument('--repo', help='Path to source code repo')
    parser.add_argument('--base-repo', help='Path to baseline source code repo')
    parser.add_argument('--target-repo', help='Path to comparison source code repo')
    parser.add_argument('--output', '-o', help='Output report file path (.md or .html)')
    parser.add_argument('--min-size', type=float, default=10.0,
                        help='Minimum allocation size in MB (default: 10.0)')
    parser.add_argument('--timeout', '-t', type=int, default=1800,
                        help='Analysis timeout in seconds (default: 1800)')
    parser.add_argument('--no-source', action='store_true',
                        help='Skip source grep; reason from function names only')
    parser.add_argument('--dry-run', action='store_true',
                        help='Print the prompt and exit without running')
    parser.add_argument('--worklogs', action='store_true',
                        help='Enable verbose debug worklogs (~500-700 extra tokens/agent)')
    parser.add_argument('--model',
                        help='Model to use (e.g. gpt-5.5, claude-opus-4.6, gemini-3.1-pro). '
                             'Passed to CLI via --model flag.')

    args = parser.parse_args()

    if not os.path.exists(args.data_file):
        print(f"Error: Data file not found: {args.data_file}", file=sys.stderr)
        return 1

    if args.no_source:
        base_repo = args.repo or args.base_repo or ""
        target_repo = args.repo or args.target_repo or base_repo
        if base_repo and not os.path.exists(base_repo):
            base_repo = ""
        if target_repo and not os.path.exists(target_repo):
            target_repo = ""
    elif args.repo:
        base_repo = target_repo = args.repo
    elif args.base_repo and args.target_repo:
        base_repo = args.base_repo
        target_repo = args.target_repo
    else:
        print("Error: Must specify --repo or both --base-repo/--target-repo "
              "(or --no-source)", file=sys.stderr)
        return 1

    if not args.no_source:
        for label, path in [("Base repo", base_repo), ("Target repo", target_repo)]:
            if not os.path.exists(path):
                print(f"Error: {label} not found: {path}", file=sys.stderr)
                return 1

    output_file = args.output or f"memory_analysis_report_{datetime.now().strftime('%Y%m%d_%H%M%S')}.md"
    output_file = os.path.abspath(output_file)
    output_html = output_file.lower().endswith('.html')
    md_output_file = output_file[:-5] + '.md' if output_html else output_file

    if args.dry_run:
        mode = detect_file_mode(args.data_file)
        worklog_dir = _worklog_dir_for(os.path.abspath(md_output_file))
        prompt = build_prompt(md_output_file, base_repo, target_repo, args.min_size,
                              mode=mode, data_file=args.data_file,
                              no_source=args.no_source, worklog_dir=worklog_dir,
                              worklogs_enabled=args.worklogs)
        cli_cmd = find_cli_command() or "<no CLI found>"
        model_flag = f" --model {args.model}" if args.model else ""

        print("=" * 80)
        print("DRY RUN")
        print("=" * 80)
        print(f"CLI: {cli_cmd}{model_flag}")
        print(f"Data: {args.data_file} [mode: {mode}]")
        print(f"Min size: {args.min_size} MB | No source: {args.no_source}")
        print(f"Output: {md_output_file}")
        print(f"Prompt ({len(prompt.encode('utf-8')) / 1024:.1f} KB):")
        print("-" * 80)
        print(prompt)
        return 0

    success = run_analysis(
        args.data_file, base_repo, target_repo, md_output_file,
        min_size_mb=args.min_size, timeout=args.timeout,
        no_source=args.no_source, worklogs_enabled=args.worklogs,
        model=args.model or "",
    )

    if success and output_html:
        if HTML_CONVERSION_AVAILABLE:
            print("Converting markdown to HTML...")
            if convert_md_to_html(md_output_file, output_file):
                print(f"HTML report: {output_file}")
            else:
                print(f"Warning: HTML conversion failed, markdown at: {md_output_file}",
                      file=sys.stderr)
        else:
            print(f"Warning: HTML conversion not available", file=sys.stderr)

    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
