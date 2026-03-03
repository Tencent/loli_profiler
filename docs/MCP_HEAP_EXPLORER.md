# MCP Heap Explorer

## Overview

The MCP (Model Context Protocol) Heap Explorer provides an interactive way to analyze LoliProfiler heap data files using Claude Code. It supports two file formats:

- **Diff files** from `LoliProfilerCLI --compare` — two-profile comparison showing memory growth/shrinkage
- **Snapshot files** from `LoliProfilerCLI --dump` — single-profile export showing absolute memory distribution

Instead of dumping the entire file content (often 12MB+ for diffs, or 100MB+ for snapshots) into a single prompt, it loads the data into an in-memory tree and exposes query tools that Claude can call interactively.

![](images/heap_callstack_mcp.png)

This approach reduces context usage from megabytes to ~10-20KB of focused, on-demand responses.

### How It Works

```
data file (diff.txt or snapshot.txt)
    |
    v
MCP Server (heap_explorer_server.py)
    |  Auto-detects format (diff vs snapshot)
    |  Loads into indexed in-memory tree
    |  Exposes 7 query tools via stdio
    v
Claude Code
    |  Calls load_file() -> loads data (if not pre-loaded)
    |  Calls get_summary() -> understands situation
    |  Calls get_top_allocations() -> finds hotspots
    |  Calls get_children() / get_call_path() -> drills down
    |  Greps/Reads source code -> understands implementation
    v
Analysis Report (markdown/HTML)
```

### Comparison with analyze_memory_diff.py

| Aspect | analyze_memory_diff.py | analyze_heap.py (MCP) |
|--------|----------------------|----------------------|
| Data in prompt | Full file content (MB) | ~2KB instruction only |
| Context usage | Saturates window | ~10-20KB across tool calls |
| Max file size | Limited by context window | Unlimited (tested 588K+ nodes) |
| Analysis style | One-shot | Interactive exploration |
| Source code search | Yes | Yes |
| Snapshot support | No | Yes |

## Prerequisites

Install the MCP Python SDK:

```bash
pip install "mcp[cli]>=1.0.0"
```

Or use the included requirements file:

```bash
pip install -r requirements.txt
```

## Two Ways to Use

### 1. Interactive Mode (Claude Code Session)

The `.mcp.json` in the project root starts the MCP server with no file pre-loaded:

```json
{
  "mcpServers": {
    "loli-heap": {
      "command": "python",
      "args": ["mcp_server/heap_explorer_server.py"],
      "env": {}
    }
  }
}
```

Start a Claude Code session in the project directory. The MCP tools are available automatically. Just tell Claude which file to analyze:

```
> Analyze heap_7682.txt for the top memory hotspots.
```

Claude will call `load_file("heap_7682.txt")` to load the data, then proceed with analysis using the other tools.

You can also switch files mid-session — calling `load_file` again replaces the previous data.

**Pre-loading a file** is still supported if you prefer. Add `--file` to the args in `.mcp.json`:

```json
"args": ["mcp_server/heap_explorer_server.py", "--file", "path/to/data.txt"]
```

### 2. Automated Batch Mode (analyze_heap.py)

For CI/CD or scripted analysis, use `analyze_heap.py` which launches Claude Code as a subprocess with the MCP server pre-configured:

```bash
# Analyze a diff (same codebase for both profiles)
python analyze_heap.py diff.txt --repo /path/to/game/source -o report.md

# Analyze a heap snapshot
python analyze_heap.py snapshot.txt --repo /path/to/game/source -o report.md

# HTML output
python analyze_heap.py diff.txt --repo /path/to/game/source -o report.html

# Different repos for baseline vs comparison (diff only)
python analyze_heap.py diff.txt --base-repo /path/to/v1 --target-repo /path/to/v2

# Custom minimum size threshold
python analyze_heap.py diff.txt --repo /path/to/source --min-size 1.0
```

## MCP Tools Reference

The server exposes 7 tools. Each returns text with `[node_id]` references for follow-up queries. All tools work identically for both diff and snapshot files.

### load_file

Load a heap data file into the explorer. Replaces any previously loaded data. Accepts absolute paths or paths relative to the working directory.

**Parameters:**
- `file_path` (str, required) - Path to `.txt` file from `LoliProfilerCLI --compare` or `--dump`.

```
Loaded 588,473 nodes (230 roots) from heap_7682.txt [mode: snapshot]
```

**When to use:** At the start of a session when no file was pre-loaded via `--file`, or to switch to a different file mid-session.

### get_summary

Returns overview statistics and tree metadata. Output adapts to the file format.

**Diff file output:**

```
=== Heap Diff Summary ===
Baseline allocations:   2,353,608
Comparison allocations:  2,369,493
Baseline total size:    534.44 MB
Comparison total size:  512.44 MB
Size delta:             -22.00 MB
Changed allocations:    45,413
New allocations:        3,494

=== Tree Metadata ===
Total nodes:            45,413
Root nodes:             53
Unique function names:  4,144

=== Top Root Nodes ===
  [0] -[IOSAppDelegate MainAppThread:], +132.19 MB, count=778693
  [29585] FRunnableThreadPThread::Run(), +55.84 MB, count=174639
```

**Snapshot file output:**

```
=== Heap Snapshot Summary ===
Total allocations:      2,655,709
Total size:             634.21 MB

=== Tree Metadata ===
Total nodes:            588,473
Root nodes:             230
Unique function names:  12,741

=== Top Root Nodes ===
  [0] 0x5e6c000219df5a10, 306.68 MB, count=1280098
  [276566] 0xb605800219df5a10, 213.58 MB, count=1015818
```

**When to use:** First call in any analysis session. Understand the scale and identify major roots.

### get_top_allocations

Find the N largest allocation nodes across all depths. For diffs this finds the largest growth points; for snapshots it finds the largest absolute allocations.

**Parameters:**
- `n` (int, default 20) - Maximum results to return.
- `min_size_mb` (float, default 0.0) - Minimum absolute size in MB.

```
Top 5 allocations (min 2.0 MB):

  [0] +132.19 MB, count=778693, depth=0 | -[IOSAppDelegate MainAppThread:]
  [1] +132.17 MB, count=778551, depth=1 | FAppEntry::Tick()
  [31162] +12.72 MB, count=57442, depth=11 | FPrimitiveSceneInfo::CacheMeshDrawCommands(...)
  [32281] +21.75 MB, count=111751, depth=0 | CAkThreadedBankMgr::ExecuteCommand()
```

**When to use:** Second call. Identifies hotspots at any depth, not just root nodes. Skip the generic wrappers (depth 0-3) and focus on functional-level functions deeper in the tree.

### get_children

List direct children of a node, sorted by absolute size descending.

**Parameters:**
- `node_id` (int, required) - The integer ID of the parent node.

```
Children of [3] UGameEngine::Tick(float, bool) (5 children):

  [4] +37.40 MB, count=87655 | UWorld::Tick(ELevelTick, float)
  [7241] +28.56 MB, count=194132 | FTickTaskManager::StartFrame(...)
  [26689] +8.01 MB, count=57473 | UGameEngine::RedrawViewports()
```

**When to use:** Drill into a specific branch. Follow the largest children to find where memory actually goes.

### get_call_path

Trace the full call path from root to a specific node.

**Parameters:**
- `node_id` (int, required) - The integer ID of the target node.

```
Call path to [31162] (12 frames):

[0] -[IOSAppDelegate MainAppThread:], +132.19 MB, count=778693
  [1] FAppEntry::Tick(), +132.17 MB, count=778551
    [2] FEngineLoop::Tick(), +132.17 MB, count=778551
      ...
          [31162] FPrimitiveSceneInfo::CacheMeshDrawCommands(...), +12.72 MB, count=57442
```

**When to use:** Understand the calling context that leads to an allocation. Useful for cross-referencing with source code.

### search_function

Regex search across all function names in the tree.

**Parameters:**
- `pattern` (str, required) - Regular expression to match.
- `max_results` (int, default 30) - Maximum results to return.

```
Found 5 matches (showing top 5 of 4305 total) for 'FMemory':

  [5947] +10.00 MB, count=160, depth=25 | FMemory::Malloc(unsigned long, unsigned int)
  [31171] +7.57 MB, count=19376, depth=20 | FMemory::Realloc(void*, unsigned long, unsigned int)
```

Results are sorted by absolute size descending, so top results are the most significant.

**When to use:** Find specific modules, classes, or patterns. Examples: `CAkBankMgr`, `Texture2D`, `NavMesh`, `Lua`.

### get_subtree

Show the indented tree structure below a node.

**Parameters:**
- `node_id` (int, required) - The root of the subtree.
- `max_depth` (int, default 4) - Maximum depth to render.

```
[32281] CAkThreadedBankMgr::ExecuteCommand(), +21.75 MB, count=111751
    [32282] CAkBankMgr::LoadBank(...), +21.75 MB, count=111751
        [32283] CAkBankMgr::ProcessDataChunk(...), +21.75 MB, count=111751
            [32284] CAkBankMgr::LoadMedia(...), +11.56 MB, count=55624
            [32310] CAkBankMgr::LoadHircChunk(...), +10.19 MB, count=56127
```

**When to use:** See the full branch structure at a glance. Good for understanding how memory distributes across sub-branches.

## Typical Analysis Workflow

0. **Load a file**: Call `load_file("data.txt")` if no file was pre-loaded via `--file`.

1. **Get overview**: Call `get_summary()` to understand the overall memory situation and tree scale.

2. **Find hotspots**: Call `get_top_allocations(20, 2.0)` to find the 20 largest nodes above 2MB. Skip generic wrappers (the first few results at depth 0-3 are usually thread entry points).

3. **Drill down each hotspot**:
   - `get_call_path(node_id)` - See how we got here
   - `get_children(node_id)` - See where memory branches below
   - `get_subtree(node_id, 5)` - See the full branch at a glance

4. **Cross-reference source code**: Use Grep/Read to find the function implementation and understand what data structures are being allocated.

5. **Search for patterns**: Use `search_function("LoadBank|LoadMedia")` to find all instances of specific allocation patterns.

## analyze_heap.py Command-Line Options

```
python analyze_heap.py <data_file> [options]
```

### Required Arguments

| Argument | Description |
|----------|-------------|
| `data_file` | Path to LoliProfilerCLI output file (diff from `--compare`, or snapshot from `--dump`) |

### Repository Options (one required)

| Option | Description |
|--------|-------------|
| `--repo <path>` | Source code repo (used for both baseline and comparison) |
| `--base-repo <path>` | Baseline version source code repo (diff only) |
| `--target-repo <path>` | Comparison version source code repo (diff only) |

### Optional Options

| Option | Default | Description |
|--------|---------|-------------|
| `-o, --output <path>` | auto-generated .md | Output report file (.md or .html) |
| `--min-size <MB>` | 2.0 | Minimum allocation size threshold in MB |
| `-t, --timeout <seconds>` | 1800 | Analysis timeout in seconds |

## Architecture

```
analyze_heap.py
  |
  |-- Detects file mode (diff vs snapshot)
  |-- Writes temp .mcp.json config
  |-- Launches: claude -p --mcp-config <config>
  |     |
  |     |-- Claude starts MCP server as child process:
  |     |     heap_explorer_server.py --file data.txt
  |     |       |
  |     |       |-- tree_model.py auto-detects format and loads data
  |     |       |-- Exposes 7 tools via FastMCP (stdio)
  |     |
  |     |-- Claude calls MCP tools interactively
  |     |-- Claude searches source code (Grep/Read)
  |     |-- Claude writes report to output file
  |
  |-- Optionally converts .md to .html
```

### File Structure

```
mcp_server/
  __init__.py                  # Package marker
  tree_model.py                # Data model + query engine (~400 lines)
  heap_explorer_server.py      # MCP server with 7 tools (~170 lines)
analyze_heap.py                # Batch automation script (~400 lines)
.mcp.json                     # Project-scoped MCP config (no --file needed)
requirements.txt               # mcp[cli]>=1.0.0
```

### tree_model.py Data Structures

**TreeNode**: Each node in the call stack tree.
- `node_id` (int) - Unique identifier for follow-up queries
- `function_name` (str) - C++ function signature
- `size_bytes` (int) - Size in bytes (signed for diffs, unsigned for snapshots)
- `size_display` (str) - Original string like "+10.44 MB" (diff) or "10.44 MB" (snapshot)
- `count` (int) - Allocation count (signed delta for diffs, absolute for snapshots)
- `level` (int) - Indentation depth (0 = root)
- `parent` / `children` - Tree links

**FileSummary**: Header statistics parsed from the report header. Contains a `mode` field (`"diff"` or `"snapshot"`) that is auto-detected, plus shared fields (`total_allocations`, `total_size`) and diff-only fields (`comparison_allocations`, `comparison_total_size`, `size_delta`, `changed_allocations`, `new_allocations`).

**CallTreeDatabase**: The main class that holds the indexed tree.
- Parses diff or snapshot files in a single pass (format auto-detected)
- Builds parent-child relationships from indentation
- Maintains a name index for search
- All query methods return formatted text strings

## End-to-End Examples

### Diff Analysis

```bash
# Step 1: Generate the diff
LoliProfilerCLI --compare baseline.loli comparison.loli --out diff.txt

# Step 2: Analyze with MCP
python analyze_heap.py diff.txt --repo /path/to/game/source -o report.md
```

### Snapshot Analysis

```bash
# Step 1: Dump a single profile to text
LoliProfilerCLI --dump profile.loli --out snapshot.txt

# Step 2: Analyze with MCP
python analyze_heap.py snapshot.txt --repo /path/to/game/source -o report.md
```

### Review the report

The report is written in Chinese markdown with function names in English, including call stacks, source code analysis, and optimization suggestions. For diff files, the report focuses on memory growth reasons; for snapshot files, it focuses on memory distribution and the largest allocation hotspots.

## Troubleshooting

### "No module named 'mcp'"

Install the MCP SDK:
```bash
pip install "mcp[cli]>=1.0.0"
```

### "claude command not found"

Ensure Claude CLI (`claude` or `claude-internal`) is installed and in your PATH.

### MCP server not connecting

Verify the server can load your data file:
```bash
python mcp_server/tree_model.py diff.txt
python mcp_server/tree_model.py snapshot.txt
```

Expected output: `Roots: <N>, Total nodes: <M>` followed by summary stats. If this fails, check that the file is a valid `LoliProfilerCLI --compare` or `--dump` output.

### Large files

The MCP server loads the entire file into memory. A 45K-node diff uses ~50MB of RAM; a 588K-node snapshot uses ~600MB. For very large files (100K+ nodes), ensure sufficient memory is available.
