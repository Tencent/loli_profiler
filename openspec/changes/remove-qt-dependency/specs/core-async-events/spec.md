## ADDED Requirements

### Requirement: Qt-free async events
Core processes and long-running operations SHALL notify completion/progress via `std::function` callbacks marshalled to the consuming thread through a thread-safe queue, replacing Qt signals/slots and the Qt event loop.

#### Scenario: Async load completion
- **WHEN** a record load or capture step completes on a worker thread
- **THEN** the UI is notified via the queue without a Qt event loop

#### Scenario: No Qt event loop dependency
- **WHEN** the GUI or CLI runs
- **THEN** no `QCoreApplication`/`QEventLoop`/`processEvents` is required to deliver core events

### Requirement: Qt-free concurrency
Parallel work (e.g. symbol translation, record loading) SHALL use `std::async`/`std::future` or an in-house thread pool, replacing `QtConcurrent`/`QFuture`/`QFutureWatcher`.

#### Scenario: Parallel symbol translation
- **WHEN** symbol translation runs on a large record set
- **THEN** it uses the new concurrency facility and produces identical results

### Requirement: Live-capture tree build
With the Qt-free bridge, the aggregated call tree SHALL be built incrementally during live capture (not only on record load), so the stacktrace tree and treemap populate during a live session.

#### Scenario: Live capture shows tree
- **WHEN** a live capture is running on a device
- **THEN** the stacktrace tree and treemap update with aggregated data without needing to save+reload
