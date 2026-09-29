"""Audit captured allocation traffic and allocator-frame coverage in a .loli."""
import collections
import struct
import sys

stream = open(sys.argv[1], "rb")

def u32():
    return struct.unpack(">I", stream.read(4))[0]

def i32():
    return struct.unpack(">i", stream.read(4))[0]

def u64():
    return struct.unpack(">Q", stream.read(8))[0]

def qstring():
    size = u32()
    return "" if size == 0xFFFFFFFF else stream.read(size).decode("utf-16-be", "replace")

assert u32() == 0xA4B3C2D1
assert i32() == 106
i32()  # max meminfo
timeline_last_time = 0
for _ in range(i32()):
    count = i32()
    if count:
        stream.seek((count - 1) * 16, 1)
        timeline_last_time = max(timeline_last_time, int(struct.unpack(">d", stream.read(8))[0]))
        stream.seek(8, 1)
intern = {u32(): qstring() for _ in range(u32())}

records = {}
for _ in range(i32()):
    uid = qstring()
    seq, timestamp, size, addr, func_addr, lib = struct.unpack(">IiiQQI", stream.read(32))
    records[uid] = (seq, timestamp, size, addr)

stack_count = i32()
stack_start = stream.tell()
for _ in range(stack_count):
    qstring()
    stream.seek(i32() * 12, 1)

symbol_map = {}
for _ in range(i32()):
    library = qstring()
    for _ in range(i32()):
        address = u64()
        name = qstring()
        symbol_map[(library, address)] = name

frees = {u64(): u32() for _ in range(i32())}
shot_count = i32()
shot_times = []
for _ in range(shot_count):
    shot_times.append(i32())
    image_size = u32()
    if image_size != 0xFFFFFFFF:
        stream.seek(image_size, 1)

markers = {
    "zstd": "ZSTD_compress_usingCDict_internal",
    "binned3": "FMallocBinned3",
    "ansi": "FMallocAnsi",
    "async_loading": "FAsyncLoadingThread::Run",
    "thread_run": "FRunnableThreadPThread::Run",
    "android_main": "android_main",
    "datatable_serialize": "UDataTable::Serialize",
    "datatable_load": "UDataTable::LoadStructData",
    "serialize_item": "UScriptStruct::SerializeItem",
}
address_sets = {
    key: {address for (library, address), name in symbol_map.items()
          if text in name and library == "libUE4.so"}
    for key, text in markers.items()
}

stats = collections.defaultdict(lambda: [0, 0, 0, 0])
zstd_sizes = []
datatable_paths = collections.Counter()
stream.seek(stack_start)
for _ in range(stack_count):
    uid = qstring()
    frames = stream.read(i32() * 12)
    record = records.get(uid)
    if record is None:
        continue
    seq, timestamp, size, addr = record
    live = seq >= frees.get(addr, 0)
    frame_addrs = {a for _lib, a in struct.iter_unpack(">IQ", frames)}
    if len(datatable_paths) < 12 and not frame_addrs.isdisjoint(address_sets["datatable_serialize"]):
        decoded = [symbol_map.get((intern.get(libhash, "").replace("\\", "/").rsplit("/", 1)[-1], address), "?")
                   for libhash, address in struct.iter_unpack(">IQ", frames)]
        positions = [i for i, name in enumerate(decoded) if "UDataTable::Serialize" in name]
        if positions:
            at = positions[0]
            datatable_paths[tuple(decoded[max(0, at - 3):at + 5])] += 1
    for key, addresses in address_sets.items():
        if frame_addrs.isdisjoint(addresses):
            continue
        item = stats[key]
        item[0] += 1
        item[1] += size
        item[2] += int(live)
        item[3] += size if live else 0
        if key == "zstd":
            zstd_sizes.append(size)

gross = sum(r[2] for r in records.values())
live_total = sum(r[2] for r in records.values() if r[0] >= frees.get(r[3], 0))
print("records", len(records), "gross_MiB", round(gross / 1048576, 2),
      "live_MiB", round(live_total / 1048576, 2), "free_addresses", len(frees),
      "timeline_last_time", timeline_last_time, "screenshots", shot_count,
      "first_last_shot_time", (shot_times[0], shot_times[-1]) if shot_times else None)
for key in markers:
    n, b, live_n, live_b = stats[key]
    print(key, "symbol_addresses", len(address_sets[key]), "records", n,
          "gross_MiB", round(b / 1048576, 2), "live_records", live_n,
          "live_MiB", round(live_b / 1048576, 2))
if zstd_sizes:
    zstd_sizes.sort()
    print("zstd_alloc_sizes", "min", zstd_sizes[0],
          "median", zstd_sizes[len(zstd_sizes) // 2], "max", zstd_sizes[-1])
for path, count in datatable_paths.most_common(4):
    print("datatable_sample_path", count, " -> ".join(path))
