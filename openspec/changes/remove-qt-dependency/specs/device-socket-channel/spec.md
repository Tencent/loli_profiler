## ADDED Requirements

### Requirement: Device socket channel
The device stacktrace channel SHALL use a Qt-free TCP client (and listener where needed) over the adb-forwarded port, replacing `QTcpSocket`/`QTcpServer`, with byte-identical protocol behavior to the injected library.

#### Scenario: Connect and receive stacktrace stream
- **WHEN** the engine connects to the device server over the forwarded port during capture
- **THEN** it receives the stacktrace/record stream correctly and incrementally

#### Scenario: Connection loss handling
- **WHEN** the device connection drops mid-capture
- **THEN** the engine detects the loss and surfaces it (matching prior ConnectionLost behavior)

#### Scenario: Protocol unchanged
- **WHEN** capturing from a real device with the existing injected library
- **THEN** the framing/bytes exchanged are identical to the QTcpSocket implementation

### Requirement: Socket backend
The socket implementation SHALL use SFML networking (already vendored) or an equivalent in-house wrapper, and MUST NOT introduce a new external dependency beyond what is already vendored.

#### Scenario: No new dependency
- **WHEN** the build is configured
- **THEN** the socket channel links only against already-vendored libraries
