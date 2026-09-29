# agentcli

The installed `agentcli` and `loli` commands run the same analysis CLI;
`loli` remains for existing scripts.

A command-line tool for analyzing LoliProfiler heap snapshots. It reads:

- `.loli` (raw capture, auto-converted on first use to a SQLite `.db` cache)
- `.db` (SQLite snapshot produced by `LoliProfilerCLI --dump foo.loli --out foo.db`)

Designed to be lightweight to deploy in any environment that can shell out
— CI, headless runners, sandboxed containers, cron, remote shells, AI agents.

Backed by SQLite: every subcommand is a single indexed query, so the cost
of opening the snapshot is paid **once at conversion time** and not on
every invocation. A typical subcommand returns in ~600 ms on a 1.5M-node
profile (most of which is Python startup); regex `search` over every
function name takes a few seconds.

## Setup

### Prerequisites

- Python 3.10+
- `LoliProfilerCLI` available somewhere we can find it (env var
  `LOLI_PROFILER_CLI`, repo root, or system `PATH`) — only required if
  you pass `.loli` files (we auto-convert them to `.db` once and cache
  the result on disk).

### Install

```bash
pip install -e .
```

You should install from the repo root, not from this directory.

You can also run it without installing:

```bash
python -m agentcli.cli --help
```

## Quick start

```bash
# 1. Discoverability — JSON-shaped help for agents
loli describe
loli describe top

# 2. Summary on a raw .loli capture (auto-converted on first use)
loli summary /path/to/profile.loli

# 3. Top 10 hotspots over 5 MB
loli top /path/to/profile.loli -n 10 --min-size-mb 5

# 4. Drill into a node
loli children /path/to/profile.loli 0
loli call-path /path/to/profile.loli 31162
loli subtree /path/to/profile.loli 0 --max-depth 3

# 5. Search by regex (case-insensitive, full Python re syntax)
loli search /path/to/profile.loli "FMemory|Realloc" --max 10

# 6. JSON mode for machine consumption
loli top /path/to/profile.loli -n 5 --json | jq '.results'
```

## Subcommand reference

All subcommands take the heap file path as the first positional argument.
`<file>` may be a `.loli` (auto-converted) or a `.db` (read directly).

| Command | Description |
|---|---|
| `loli describe [<cmd>]` | Print the full schema as JSON. **Run this first if you're an agent.** |
| `loli load-file <file>` | Validate the file; report node/root counts. |
| `loli summary <file>` | Header stats, tree metadata, top 5 roots. |
| `loli top <file> [-n N] [--min-size-mb F]` | Top-N largest allocation nodes across all depths. |
| `loli children <file> <node_id>` | Direct children of a node, by size descending. |
| `loli call-path <file> <node_id>` | Trace from root to node. |
| `loli search <file> <pattern> [--max N]` | Regex search across function names. |
| `loli subtree <file> <node_id> [--max-depth N]` | Indented subtree view. |

Every command except `describe` accepts `--json` for machine-readable
output. `describe` always emits JSON.

## How `.loli` auto-conversion works

When you point any subcommand at a `.loli` file:

1. `loli` looks for `LoliProfilerCLI` (env var `LOLI_PROFILER_CLI` →
   repo-root sibling → system `PATH`).
2. It runs `LoliProfilerCLI --dump <file>.loli --out <file>.db`.
3. The resulting `.db` is written next to the `.loli` and reused for
   subsequent calls (cache check by mtime).

If two `loli` invocations race on a missing `.db`, an `O_EXCL` lockfile
(`<file>.db.lock`) serializes them: the first does the conversion, the
others wait and reuse the cached result.

If `LoliProfilerCLI` isn't available, set the env var or pre-convert
manually:

```bash
LoliProfilerCLI --dump profile.loli --out profile.db
```

## SQLite database layout

The `.db` file is plain SQLite — open it with `sqlite3`, the Python
`sqlite3` stdlib module, or any other SQLite client. The tree is fully
materialized at conversion time (resolved symbols included), so every
query is a single indexed SELECT or recursive CTE.

### Schema

```sql
metadata(key TEXT PRIMARY KEY, value TEXT);
  -- Stamped at conversion time. See "Metadata keys" below.

libraries(id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL);
  -- Library name interned to a small integer id.

symbols(library_id INTEGER NOT NULL,
        address    INTEGER NOT NULL,
        name       TEXT    NOT NULL,
        PRIMARY KEY(library_id, address)) WITHOUT ROWID;
  -- The full symbol map from the .loli file. Forensic only — not used
  -- by the CLI hot path. Lets external tools re-resolve addresses
  -- without re-parsing the .loli.

nodes(id            INTEGER PRIMARY KEY,
      parent_id     INTEGER REFERENCES nodes(id),  -- NULL for roots
      depth         INTEGER NOT NULL,              -- 0 for roots
      function_name TEXT    NOT NULL,              -- already-resolved symbol or "lib!0xADDR"
      library_id    INTEGER REFERENCES libraries(id),
      func_addr     INTEGER NOT NULL,              -- raw quint64; informational only
      size_bytes    INTEGER NOT NULL,
      count         INTEGER NOT NULL);

CREATE INDEX idx_nodes_parent ON nodes(parent_id);
CREATE INDEX idx_nodes_size   ON nodes(size_bytes DESC);
```

### Metadata keys

| Key | Value |
|---|---|
| `magic` | Always the string `loli`. Reject the file if missing. |
| `schema_version` | `1`. Bumped on incompatible schema changes. |
| `app_version` | The .loli file format version (currently `106`). |
| `mode` | `snapshot`. (Diff mode is not yet exported to SQLite.) |
| `total_allocations` | Live allocation count from the source `.loli`. |
| `total_size_bytes` | Live byte total from the source `.loli`. |
| `total_size_display` | Human-readable form (e.g. `1.76 GB`). |
| `skip_root_levels` | The `--skip-root-levels` value used during conversion. |
| `node_count` | Total rows in `nodes`. |
| `root_count` | Number of rows where `parent_id IS NULL`. |
| `unique_function_names` | Distinct `function_name` strings across all nodes. |
| `traversal` | `size_desc_v1` — node ids are assigned in DFS pre-order, sorting siblings by `size_bytes DESC` with `function_name` / `library` / `func_addr` as deterministic tiebreaks. |
| `created_at` | ISO-8601 UTC timestamp of conversion. |

### ID stability

Node IDs are deterministic — converting the same `.loli` twice produces
identical IDs (same DFS order, same tiebreaks). This means agents can
persist `[node_id]` references across calls without worrying about
renumbering.

### Useful one-liners

```bash
# Top 10 root nodes
sqlite3 profile.db "
  SELECT id, function_name, size_bytes, count
  FROM nodes WHERE parent_id IS NULL
  ORDER BY size_bytes DESC LIMIT 10;
"

# Walk the call path for a specific node (recursive CTE)
sqlite3 profile.db "
  WITH RECURSIVE chain(id, function_name, parent_id, depth) AS (
    SELECT id, function_name, parent_id, depth FROM nodes WHERE id = 5
    UNION ALL
    SELECT n.id, n.function_name, n.parent_id, n.depth
    FROM nodes n JOIN chain c ON n.id = c.parent_id
  )
  SELECT depth, function_name FROM chain ORDER BY depth ASC;
"

# Total bytes attributed to a library
sqlite3 profile.db "
  SELECT SUM(size_bytes) FROM nodes WHERE library_id =
    (SELECT id FROM libraries WHERE name = 'libUE4.so');
"

# Find every node whose name matches a pattern
sqlite3 profile.db "
  SELECT id, function_name, size_bytes
  FROM nodes WHERE function_name LIKE '%Tick%'
  ORDER BY size_bytes DESC LIMIT 20;
"
```

## Programmatic use

`agentcli.core` exports the same surface as a Python API. Each function
returns `{"text": "<formatted text>", "data": <structured>}`:

```python
from agentcli import core

result = core.summary("profile.loli")
print(result["text"])                  # same as `loli summary profile.loli`
nodes = result["data"]["top_roots"]    # structured payload
```

For lower-level access, open the `.db` directly:

```python
from agentcli.tree_model import CallTreeDatabase

db = CallTreeDatabase()
db.load_from_file("profile.db")
print(db.node_count, db.summary.total_size)
for node in db.children_of(0):
    print(node)
```

This is useful for custom analysis scripts that want both the
human-readable rendering and the underlying structured data.
