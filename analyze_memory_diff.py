#!/usr/bin/env python3
"""
LoliProfiler Memory Diff Analyzer
Uses Claude AI to analyze memory allocation differences between two app runs
and provide insights into memory growth patterns and optimization suggestions.

This version passes the raw diff.txt file directly to Claude, allowing it to
navigate the call stack tree structure and identify functional-level allocation
points rather than just reporting high-level thread entry points.
"""

import os
import subprocess
import sys
import argparse
import shutil
from datetime import datetime

# Import preprocessing module
try:
    from preprocess_memory_diff import preprocess_diff
    PREPROCESSING_AVAILABLE = True
except ImportError:
    PREPROCESSING_AVAILABLE = False
    print("Warning: preprocess_memory_diff.py not found, large file preprocessing disabled", file=sys.stderr)


def analyze_memory_diff(diff_file: str,
                        base_repo_path: str,
                        target_repo_path: str,
                        output_file: str,
                        min_size_mb: float = 2.0,
                        timeout: int = 1800) -> bool:
    """
    Run Claude to analyze memory diff and generate a report.

    Args:
        diff_file: Path to LoliProfilerCLI generated diff.txt
        base_repo_path: Path to baseline version source code (git/svn repo)
        target_repo_path: Path to comparison version source code (git/svn repo)
        output_file: Path to write the analysis report
        min_size_mb: Minimum memory growth threshold in MiB (default 2.0)
        timeout: Analysis timeout in seconds (default 1800 = 30 minutes)

    Returns:
        True if analysis succeeded, False otherwise
    """

    # Check if claude command exists
    claude_cmd = shutil.which('claude-internal')
    if not claude_cmd:
        claude_cmd = shutil.which('claude')
    if not claude_cmd:
        print("ERROR: 'claude-internal' or 'claude' command not found!", file=sys.stderr)
        print("", file=sys.stderr)
        print("Please ensure:", file=sys.stderr)
        print("  1. Claude CLI is installed", file=sys.stderr)
        print("  2. The command is in your system PATH", file=sys.stderr)
        return False

    print(f"Using Claude command: {claude_cmd}")
    print(f"Diff file: {diff_file}")

    # Read the raw diff file content
    with open(diff_file, 'r', encoding='utf-8') as f:
        diff_content = f.read()

    # Check file size and preprocess if too large
    diff_size_mb = len(diff_content) / (1024 * 1024)
    print(f"Diff file size: {diff_size_mb:.2f} MB")

    preprocessed = False
    if diff_size_mb > 0.5 and PREPROCESSING_AVAILABLE:
        print(f"\nLarge diff file detected ({diff_size_mb:.1f} MB)")
        print("Running intelligent preprocessing to extract functional-level call stacks...")
        print(f"Filtering threshold: >= {min_size_mb} MB per call stack\n")

        try:
            # Preprocess the diff content
            diff_content = preprocess_diff(diff_content, min_size_mb, verbose=True)
            preprocessed = True

            filtered_size_mb = len(diff_content) / (1024 * 1024)
            reduction_pct = (1 - filtered_size_mb / diff_size_mb) * 100 if diff_size_mb > 0 else 0

            print(f"\nPreprocessing complete!")
            print(f"Filtered size: {filtered_size_mb:.2f} MB (reduction: {reduction_pct:.1f}%)")
            print()
        except Exception as e:
            print(f"Warning: Preprocessing failed: {e}", file=sys.stderr)
            print("Falling back to raw diff file...\n", file=sys.stderr)
            preprocessed = False
    elif diff_size_mb > 0.5 and not PREPROCESSING_AVAILABLE:
        print(f"\nWarning: Large diff file ({diff_size_mb:.1f} MB) but preprocessing unavailable")
        print("Consider installing preprocess_memory_diff.py for better results\n")
    else:
        print(f"Diff file size is manageable, using raw content\n")

    # Determine if repos are same or different
    same_repo = os.path.normpath(base_repo_path) == os.path.normpath(target_repo_path)

    if same_repo:
        repo_instruction = f"""SOURCE CODE REPOSITORY (READ-ONLY):
- Source Code: {base_repo_path}

The baseline and comparison profiles are from different runs/builds of the same codebase.
Focus on understanding what code paths lead to memory growth and potential optimization."""
    else:
        repo_instruction = f"""SOURCE CODE REPOSITORIES (READ-ONLY):
- Baseline Source Code: {base_repo_path}
- Comparison Source Code: {target_repo_path}

The profiles are from different versions. You can diff files between repos to see what changed."""

    # Create prompt for Claude with the diff file (preprocessed or raw)
    if preprocessed:
        # Simplified prompt for preprocessed data
        prompt = f"""You are analyzing a memory profiling comparison report from LoliProfiler.
This report shows the DIFFERENCE in memory allocations between two runs of a mobile app.

{repo_instruction}

FILTERED MEMORY DIFF FILE (Pre-processed functional-level allocations):
================================================================================
{diff_content}
================================================================================

IMPORTANT: This is READ-ONLY analysis. DO NOT modify any files.

ABOUT THIS DATA:
This data has been intelligently pre-filtered to show only functional-level call stacks.
Each entry represents a significant memory allocation (>= {min_size_mb} MB) at application logic level.

The filtering has already:
- Removed low-level primitives (FMemory::, TArray::, operator new, etc.)
- Removed generic thread wrappers (FRunnableThreadPThread::Run, etc.)
- Climbed up to functional-level functions that describe actual game/app logic
- Merged duplicate call stacks

FORMAT:
- Tree structure with indentation (4 spaces per level)
- Each line: "FunctionName(), +SIZE MB, +COUNT"
- Root nodes show the top-level context (may still include some thread wrappers for context)
- Deeper nodes show the actual functional allocation points

YOUR TASK:
1. For each functional-level function listed in the filtered data:
   - Search for the function in source code (use Grep tool)
   - Read the implementation (use Read tool)
   - Identify what data structures are being allocated
   - Understand WHY memory is growing at this specific point

2. Provide specific, code-level optimization suggestions with examples

3. Prioritize by:
   - Memory impact (larger allocations first)
   - Implementation difficulty (quick wins vs. major refactors)
   - Likelihood of being a real issue vs. expected behavior

OUTPUT FORMAT (Chinese report):

Generate a DETAILED report with this structure:

# 内存分析报告

生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
分析工具: LoliProfiler
基线版本: {os.path.basename(base_repo_path)}
对比版本: {os.path.basename(target_repo_path)}

## 主要内存增长点分析

### [1] [函数名] - [内存增长量]

**完整调用栈:**
```
[完整的调用栈路径，从diff文件中提取]
```

**代码位置:** [file:line] (通过Grep在源码中查找)

**源码分析:**
[通过Read工具读取源码后，说明这段代码在做什么，分配了什么数据结构]

**增长原因:**
[结合源码分析，解释为什么会产生这么多内存分配]

**优化建议:**
[具体可行的优化方向，最好附带代码示例]

---

### [2] [下一个函数] - [内存增长量]
...

## 优化优先级建议

[按照影响大小和实施难度，给出优化的优先级排序]

## 总结

[整体内存增长趋势分析和关键建议]

IMPORTANT GUIDELINES:
- Use Grep tool to search for function names in source code
- Use Read tool to examine actual implementation
- Use Bash only for read-only git commands (git log, git blame) if needed
- Focus on CODE-LEVEL analysis with specific examples
- Each analysis section should be detailed (5-10 lines with code context)
- Report in Chinese, keep code/function names in English
- DO NOT MODIFY any source code files - only write to the output report file
- Analyze ALL significant memory growth points in the filtered data

CRITICAL OUTPUT REQUIREMENT:
You MUST save the complete analysis report to this file: {output_file}
Use the Write tool to save the report directly - do NOT just print it.
After saving the report, confirm with: "Report saved to: {output_file}"

The report must be complete and self-contained, including ALL detailed analysis.
Structure the report as full markdown starting with "# 内存分析报告".
"""
    else:
        # Original detailed prompt for raw data
        prompt = f"""You are analyzing a memory profiling comparison report from LoliProfiler.
This report shows the DIFFERENCE in memory allocations between two runs of a mobile app.

{repo_instruction}

MEMORY DIFF FILE (Raw tree-structured call stack data):
================================================================================
{diff_content}
================================================================================

IMPORTANT: This is READ-ONLY analysis. DO NOT modify any files.

UNDERSTANDING THE DIFF FILE FORMAT:
- The file is structured as a tree with indentation (4 spaces per level)
- Each line format: "FunctionName(), +SIZE MB, +COUNT"
- Root nodes (no indentation) are top-level call stack entries (often thread entry points)
- Child nodes (indented) show the call hierarchy going deeper
- The SIZE delta shows memory growth at that call stack level
- The COUNT delta shows number of allocation changes

CRITICAL ANALYSIS STRATEGY - FIND FUNCTIONAL-LEVEL ALLOCATION POINTS:

The diff file contains a TREE STRUCTURE. Your job is to NAVIGATE this tree to find
the FUNCTIONAL-LEVEL functions that actually describe what is being allocated.

IGNORE these high-level generic entry points (they tell us NOTHING useful):
- FRunnableThreadPThread::Run() - just a pthread wrapper
- -[IOSAppDelegate MainAppThread:] - just iOS main thread entry
- FEngineLoop::Tick() - just the main game loop
- FRenderingThread::Run() - just rendering thread entry
- FNamedTaskThread::ProcessTasksUntilQuit() - just task thread loop
- FNamedTaskThread::ProcessTasksNamedThread() - just task processing
- TGraphTask<...>::ExecuteTask() - just task graph execution
- Any generic "Run()", "Execute()", "Update()", "DoWork()" entry points

INSTEAD, drill down the tree to find FUNCTIONAL-LEVEL functions like:
- FPrimitiveSceneInfo::CacheMeshDrawCommands() - mesh command caching
- FPrimitiveSceneInfo::UpdateStaticMeshes() - mesh updates
- FMobileSceneRenderer::InitViews() - scene view initialization
- FStaticMeshSceneProxy::CreateRenderThreadResources() - mesh resources
- CAkBankMgr::LoadBank() - audio bank loading
- CAkBankMgr::ProcessDataChunk() - audio data processing
- FStreamableManager::RequestAsyncLoad() - asset streaming
- UTexture2D::UpdateResource() - texture updates
- FNavMeshPath::GeneratePath() - navigation generation
- FSkeletalMeshObjectGPUSkin::Update() - skeletal mesh updates

HOW TO ANALYZE THE TREE:
1. Start at root nodes with significant memory delta (>= {min_size_mb} MB)
2. Follow the indented children to see where memory is actually allocated
3. Keep drilling down until you find a function that describes SPECIFIC WORK
4. That functional-level function is what should be reported, NOT the root entry point
5. Look for the DEEPEST function in the tree that still has significant memory delta

EXAMPLE ANALYSIS:
If you see:
```
FRunnableThreadPThread::Run(), +18.44 MB, +79446
    FRenderingThread::Run(), +18.44 MB, +79446
        RenderingThreadMain(FEvent*), +18.44 MB, +79446
            FNamedTaskThread::ProcessTasksUntilQuit(int), +18.44 MB, +79446
                ...
                    FPrimitiveSceneInfo::CacheMeshDrawCommands(...), +4.51 MB, +1234
```

You should report "FPrimitiveSceneInfo::CacheMeshDrawCommands" as the allocation point,
NOT "FRunnableThreadPThread::Run" which is just a generic thread wrapper.

TASK:
1. Parse the tree structure in the diff file
2. For each significant memory growth branch (>= {min_size_mb} MB at root):
   - Navigate DOWN the tree following the call hierarchy
   - Find the FUNCTIONAL-LEVEL function that describes the actual allocation
   - This is typically where the size delta is still significant but the function
     name describes a specific operation (rendering, audio, physics, AI, etc.)
3. Search for these functional-level functions in the source code
4. Analyze what data structures are being allocated and why
5. Provide specific optimization suggestions

OUTPUT FORMAT (Chinese report):

Generate a CONCISE report with this structure:

# 内存分析报告

生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
分析工具: LoliProfiler
基线版本: {os.path.basename(base_repo_path)}
对比版本: {os.path.basename(target_repo_path)}

## 主要内存增长点分析

### [1] [FUNCTIONAL-LEVEL函数名] - [内存增长量]

**完整调用栈:**
```
[从root到该函数的简化调用栈，省略中间的泛型包装器]
```

**代码位置:** [file:line] (通过Grep在源码中查找)

**增长原因:** [1-2句话说明这个函数为什么分配内存，分配了什么数据结构]

**优化建议:** [具体可行的优化方向]

---

### [2] [下一个FUNCTIONAL-LEVEL函数] - [内存增长量]
...

## 总结

[整体内存增长趋势分析和优化优先级建议]

IMPORTANT GUIDELINES:
- Use Read tool to examine source code
- Use Grep to search for function names in the source code
- Use Bash only for read-only git/svn commands (git log, git blame, svn log)
- Focus on FUNCTIONAL-LEVEL functions, NOT thread entry points
- Report should have actionable insights, not generic advice
- Keep each analysis section concise (3-5 lines max)
- Report in Chinese, keep code/function names in English
- DO NOT MODIFY any source code files - only write to the output report file
- Analyze at least 5-10 significant memory growth points if available

CRITICAL OUTPUT REQUIREMENT:
You MUST save the complete analysis report to this file: {output_file}
Use the Write tool to save the report directly - do NOT just print it.
After saving the report, confirm with: "Report saved to: {output_file}"

The report must be complete and self-contained, including ALL detailed analysis.
Structure the report as full markdown starting with "# 内存分析报告".
"""

    print(f"Passing raw diff file to Claude for analysis...")
    print(f"Analysis will take approximately {timeout // 60} minutes...")
    print()

    try:
        is_windows = sys.platform.startswith('win')

        # Run Claude with access to the repos
        result = subprocess.run(
            [claude_cmd, '-p', '--verbose', '--dangerously-skip-permissions'],
            input=prompt,
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='replace',
            timeout=timeout,
            cwd=target_repo_path if os.path.isdir(target_repo_path) else base_repo_path,
            shell=is_windows
        )

        if result.returncode != 0:
            print(f"Claude analysis failed: {result.stderr}", file=sys.stderr)
            return False

        report = result.stdout.strip()

        if not report:
            print("Claude returned empty response", file=sys.stderr)
            return False

        # Check if Claude already wrote the report file (preferred behavior)
        if os.path.exists(output_file):
            # Read the report that Claude wrote
            with open(output_file, 'r', encoding='utf-8') as f:
                report = f.read()
            print()
            print("ANALYSIS COMPLETE")
            print(f"Report written by Claude to: {output_file}")
        else:
            # Fallback: Write Claude's stdout to file
            with open(output_file, 'w', encoding='utf-8') as f:
                f.write(report)
            print()
            print("ANALYSIS COMPLETE")
            print(f"Report written to: {output_file}")
        print()
        print("Report preview:")
        print("-" * 80)

        # Print first 60 lines of report
        lines = report.split('\n')
        for line in lines[:60]:
            print(line)
        if len(lines) > 60:
            print(f"\n... ({len(lines) - 60} more lines in full report)")
        print()

        return True

    except subprocess.TimeoutExpired:
        print(f"Claude analysis timed out after {timeout // 60} minutes", file=sys.stderr)
        return False
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return False


def main():
    parser = argparse.ArgumentParser(
        description='Analyze LoliProfiler memory diff using Claude AI to identify memory growth causes and optimization opportunities',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Analyze with same repo for both versions
  python analyze_memory_diff.py diff.txt --repo /path/to/game/source

  # Analyze with different repos for baseline and comparison
  python analyze_memory_diff.py diff.txt --base-repo /path/to/v1 --target-repo /path/to/v2

  # Custom threshold and output
  python analyze_memory_diff.py diff.txt --repo /path/to/source --min-size 1.0 -o report.md
        """
    )

    parser.add_argument('diff_file',
                        help='Path to LoliProfilerCLI generated diff.txt file')
    parser.add_argument('--repo',
                        help='Path to source code repo (used for both baseline and comparison)')
    parser.add_argument('--base-repo',
                        help='Path to baseline version source code repo')
    parser.add_argument('--target-repo',
                        help='Path to comparison version source code repo')
    parser.add_argument('--output', '-o',
                        help='Output report file path (default: memory_analysis_report_YYYYMMDD_HHMMSS.md)')
    parser.add_argument('--min-size', type=float, default=2.0,
                        help='Minimum memory growth threshold in MiB (default: 2.0)')
    parser.add_argument('--timeout', '-t', type=int, default=1800,
                        help='Analysis timeout in seconds (default: 1800 = 30 minutes)')

    args = parser.parse_args()

    # Validate inputs
    if not os.path.exists(args.diff_file):
        print(f"Error: Diff file not found: {args.diff_file}", file=sys.stderr)
        return 1

    # Determine repo paths
    if args.repo:
        base_repo = target_repo = args.repo
    elif args.base_repo and args.target_repo:
        base_repo = args.base_repo
        target_repo = args.target_repo
    else:
        print("Error: Must specify either --repo or both --base-repo and --target-repo", file=sys.stderr)
        return 1

    if not os.path.exists(base_repo):
        print(f"Error: Base repo path not found: {base_repo}", file=sys.stderr)
        return 1

    if not os.path.exists(target_repo):
        print(f"Error: Target repo path not found: {target_repo}", file=sys.stderr)
        return 1

    # Generate output filename (ensure absolute path for Claude)
    output_file = args.output or f"memory_analysis_report_{datetime.now().strftime('%Y%m%d_%H%M%S')}.md"
    output_file = os.path.abspath(output_file)

    # Run analysis
    success = analyze_memory_diff(
        args.diff_file,
        base_repo,
        target_repo,
        output_file,
        min_size_mb=args.min_size,
        timeout=args.timeout
    )

    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
