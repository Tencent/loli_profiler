## ADDED Requirements

### Requirement: Cross-platform process runner
The system SHALL provide a process-execution facility for adb and NDK tools that supports: setting program + arguments + working directory, capturing stdout and stderr, blocking wait with timeout, asynchronous completion notification, and process termination — on Windows and POSIX, without Qt.

#### Scenario: Run adb command and capture output
- **WHEN** the engine runs `adb devices` (or any adb/NDK subprocess)
- **THEN** it captures the process stdout/stderr and exit status correctly

#### Scenario: Async process completion
- **WHEN** a long-running subprocess is launched asynchronously
- **THEN** the caller is notified on completion without blocking the UI thread

#### Scenario: Kill a running process
- **WHEN** the engine terminates an in-flight subprocess (e.g. stop capture)
- **THEN** the process is killed and its resources released

### Requirement: Replaces QProcess
All adb/NDK invocations that previously used `QProcess` SHALL use the new process runner, with identical command lines and output handling.

#### Scenario: adb command parity
- **WHEN** any adb or NDK tool is invoked through the new runner
- **THEN** the command, arguments, working directory, and captured output are identical to the prior QProcess-based behavior
