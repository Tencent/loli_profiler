## ADDED Requirements

### Requirement: Platform-native GUI shortcuts
The GUI SHALL use Command on macOS and Control on Windows/Linux for Open, Run, Save, Settings, and Quit, with menu labels matching actual keyboard behavior. It SHALL use ImGui's normalized modifiers and retain native macOS text-editing shortcuts. Native file dialogs SHALL clear stale input keys when they return.

#### Scenario: macOS application shortcut
- **WHEN** the user presses Command+O, Command+R, Command+S, Command+comma, or Command+Q while the action is available
- **THEN** the corresponding Open, Run, Save, Settings, or Quit action triggers
- **AND** physical Control alone does not trigger those macOS application shortcuts

#### Scenario: Windows or Linux application shortcut
- **WHEN** the user presses Control with the corresponding application key
- **THEN** the action triggers and Super alone does not trigger it

### Requirement: Native macOS GUI and CLI build
The project SHALL build the Qt-free GUI app bundle and CLI with the installed Apple toolchain and pinned dependencies. Its default minimum macOS target SHALL satisfy the pinned dependencies and C++17 filesystem use, and unsupported targets SHALL fail clearly.

#### Scenario: Apple Silicon native build
- **WHEN** the native build driver runs on Apple Silicon with its prerequisites installed
- **THEN** it builds an arm64 GUI executable in `LoliProfilerImGui.app` and an arm64 `LoliProfilerCLI` without Qt

### Requirement: Executable-relative capture runtime
The build driver SHALL stage injector, logging configuration, icon, and available Android hook libraries beside the CLI and inside the GUI bundle. Full builds SHALL refresh those copies after hooks build.

#### Scenario: Full build stages fresh hooks
- **WHEN** a full macOS build finishes building all four LLVM Android hook ABIs
- **THEN** local CLI and GUI runtime paths contain copies matching the freshly built source hooks

### Requirement: Runnable extracted release
The capture-ready macOS release ZIP SHALL contain the main GUI, CLI, standalone comparison GUI, Python agent, documentation, injector, and four LLVM Android hooks, retaining executable permissions and required bundle frameworks without build-tree runtime dependencies.

#### Scenario: Extracted archive execution
- **WHEN** the archive is extracted to a separate directory
- **THEN** the CLI help and sample export succeed and the GUI loads a sample with its bundled resources

#### Scenario: Upstream comparison viewer in the macOS release
- **WHEN** the merged release is built and extracted
- **THEN** all three desktop targets build, the standalone comparison viewer renders Base/Comparer/Diff, and its report matches the shared CLI signed comparison
- **AND** the main app bundle can locate the independent comparison executable beside the bundle

### Requirement: Explicit offline native archive
The package script SHALL support an explicit native-only archive for saved-record inspection without requiring Android hooks. Its filename SHALL distinguish it from the capture-ready release, and the default full-release mode SHALL still fail when required hooks are missing. App archives SHALL omit generated settings, logs, and caches.

#### Scenario: Native package without Android tools
- **WHEN** packaging is requested with `--native-only` before Android hooks are available
- **THEN** `LoliProfiler-macos-native.zip` contains the runnable GUI/CLI and offline agent/runtime resources without stale staged hooks or generated application state

### Requirement: macOS Android tool resolution
The core SHALL discover explicit SDK/NDK environment paths and side-by-side installed NDKs on macOS, use available LLVM tools for both supported ARM capture architectures, and resolve macOS symbolizers in live capture as well as offline operations.

#### Scenario: Modern side-by-side NDK
- **WHEN** an SDK has an installed modern side-by-side NDK and no legacy GCC toolchain
- **THEN** the profiler can locate its `llvm-nm` and `llvm-symbolizer` for ARMv7 and ARM64 captures

### Requirement: Python 3 capture injector
The bundled JDWP injector SHALL operate with the discovered Python 3 interpreter, separate wire bytes from decoded text, read complete packets across fragmented socket reads, and distinguish asynchronous events from command replies.

#### Scenario: Modern macOS host launches Android capture
- **WHEN** macOS uses Python 3 with a modern NDK that has no Python 2 runtime
- **THEN** the injector completes JDWP handshake, command/class/method parsing, and library loading without Python 2-only API or text/bytes errors

### Requirement: ARM64 authenticated return addresses
The Android frame-pointer hook SHALL remove pointer authentication from return PCs before validating and recording them, without requiring newer instructions on older ARM64 cores.

#### Scenario: Signed null terminates the frame chain
- **WHEN** an ARM64 frame contains a signed null return PC
- **THEN** the walker rejects the frame instead of saving a fabricated high-address root
- **AND** unsigned valid return PCs remain unchanged
