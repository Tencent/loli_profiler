# .loli Binary Format Reference (confirmed against real captures)

Confirmed empirically by parsing 6 real `.loli` files (~10M records total) to
zero trailing bytes, cross-checked with the Qt 5.14 writer/reader source
(`src/cliprofiler.cpp:SaveToFile`, `src/gui/guidatabridge.cpp` load path).

All multi-byte values are **big-endian** (QDataStream default).
Writer pins `QDataStream::Qt_5_12` (after magic/version); for every type used
here the encoding is identical across Qt 5.x versions.

## Primitive encodings

| Type | Encoding |
|---|---|
| qint32 / quint32 | 4 bytes big-endian |
| qint64 / quint64 | 8 bytes big-endian |
| double | 8 bytes IEEE-754 big-endian |
| QString | `quint32 byteLength` + UTF-16BE data (byteLength = 2 x UTF-16 code units; null string = 0xFFFFFFFF, empty = 0) |
| QByteArray | `quint32 byteLength` + raw bytes (null = 0xFFFFFFFF) |
| QPointF | two doubles (x, y) |
| QHash\<K,V\> (operator<<) | `quint32 count` + (key, value) pairs in iteration order |

## Section layout (version 106, magic 0xA4B3C2D1)

```
quint32 magic                 0xA4B3C2D1
qint32 version                106
qint32 maxMemInfoValue
qint32 seriesCount            (writer emits 6)
per series:
    qint32 pointCount
    pointCount x QPointF      (two doubles each; X is elapsed seconds)

quint32 hashCount             QHash operator<< (NOT qint32!)
hashCount x { quint32 hashcode, QString libraryName }   // string intern table

qint32 recordCount
recordCount x record:
    QString uuid              // braced QUuid::toString form: "{xxxxxxxx-...}"
    quint32 seq
    qint32 time               (elapsed milliseconds)
    qint32 size
    quint64 addr
    quint64 funcAddr
    quint32 libHash           // hashcode into intern table

qint32 callstackCount
callstackCount x:
    QString uuid
    qint32 frameCount
    frameCount x { quint32 libHash, quint64 addr }

qint32 symbolLibCount
symbolLibCount x:
    QString libName
    qint32 symbolCount
    symbolCount x { quint64 addr, QString symbolName }

qint32 freeaddrCount
freeaddrCount x { quint64 addr, quint32 seq }

qint32 screenshotCount
screenshotCount x { qint32 elapsedSeconds, QByteArray jpeg }

qint32 smapsSectionCount
smapsSectionCount x:
    QString sectionName
    qint32 addrCount
    addrCount x { quint64 start, quint64 end, quint64 offset }
    qint32 virtual, rss, pss, privateClean, privateDirty, sharedClean, sharedDirty
```

## Verified sample files (G:/SampleData/Memory/)

| File | records | callstacks | symbolLibs | freeaddr | shots | smaps | trailing |
|---|---|---|---|---|---|---|---|
| 6s_heap_0919.loli | 2408680 | 75489 | 1 | 0 | 0 | 0 | 0 |
| 6s_heap_1219.loli | 2442487 | 76190 | 1 | 0 | 0 | 0 | 0 |
| ...uam_1524_20260511...loli | 352990 | 352990 | 172 | 0 | 0 | 0 | 0 |
| ...uam_17733_20260408...loli | 494237 | 494237 | 247 | 0 | 0 | 0 | 0 |
| ...uam_18606_20260408...loli | 483763 | 483763 | 236 | 0 | 0 | 0 | 0 |
| ...uam_2487_20260511141917.loli | 364772 | 364772 | 171 | 0 | 0 | 0 | 0 |

Notes:
- Qt writes meminfo X coordinates and screenshot times in elapsed seconds;
  allocation record times are elapsed milliseconds. Early Qt-free captures
  wrote meminfo/screenshot times in milliseconds. The GUI loader detects and
  normalizes both variants, then saves Qt-compatible seconds.
- Record fixed fields after the uuid string total **32 bytes**
  (4+4+4+8+8+4). An earlier probe miscounted 28; corrected.
- The intern-table count prefix is `quint32` (QHash operator<<), while all
  app-level section counts are `qint32` (written explicitly by app code).
- Qt screenshots store JPEG (QImage save "JPG", quality 50); the Qt-free
  capture stores PNG bytes from `adb screencap -p` in the same QByteArray
  field. The six listed sample files have no screenshots or smaps sections,
  while the newer `oldui.loli` reference has 46 screenshots and the CLI
  parity capture has 14. The serializer round-trip test also covers both
  fields.
