# 快速开始

LoliProfiler 用于采集 Android 应用的原生内存分配及调用栈。桌面程序为 `LoliProfilerImGui`，`LoliProfilerCLI` 提供无界面采集和文件转换。

| 操作 | macOS | Windows / Linux |
| --- | --- | --- |
| 打开记录 | Cmd+O | Ctrl+O |
| 运行 / 启动 | Cmd+R | Ctrl+R |
| 另存记录 | Cmd+S | Ctrl+S |
| 设置 | Cmd+, | Ctrl+, |
| 退出 | Cmd+Q | Ctrl+Q |

macOS 文本输入框的全选、复制、剪切、粘贴也使用 Command 键。

## 1. 设置 Android 路径

打开 **File > Settings**，选择 Android SDK 和 NDK。SDK 中的 `platform-tools/adb` 是程序使用的 ADB 客户端；建议与 Android Studio、Unreal 使用同一份 Platform-Tools。NDK 用于符号翻译。

![当前设置窗口](images/imgui-settings.png)

连接手机并完成 USB 调试授权。ADB 客户端通常共用一个主机服务；采集期间不要执行 `adb kill-server`。

## 2. 配置采集

点击工具栏或 File 菜单中的 **Run**（Windows/Linux 为 Ctrl+R，macOS 为 Cmd+R），打开 **Run/Launch** 对话框。选择设备，输入包名，或点 **Refresh Apps** 从已安装应用中选择。进程已运行时勾选 **Attach to running app**。附加模式会等待 Activity 恢复；若一直等待，可切到后台再返回应用。

![运行窗口、设备及记录策略](images/imgui-run-launch.png)

启动时选择记录策略：

- **Live at stop (smaller file)**：默认选项，采集期间丢弃已释放分配的调用栈，只保留停止时仍存活的分配。
- **All allocations (full history)**：保存全部分配记录，用于观察累计分配流量。

点击 **Edit Configuration** 设置 ABI、Hook 库编译器、采集模式、回溯方式、`malloc`/`mmap` Hook、阈值和动态库列表。将鼠标悬停在 `(?)` 上可查看简短说明。右键白名单或黑名单添加、删除条目；双击条目直接编辑。

![当前采集配置窗口](images/imgui-capture-config.png)

UE 游戏通常从与进程一致的 ABI、`llvm`、`strict` 开始；若游戏启用了帧指针，可选 `framepointer`。白名单可填 `libUE4`。发布包中必须有相应的 `remote/<compiler>/<arch>/libloli.so`。

## 3. 采集与保存

点击 **Launch** 或 **Attach**，在应用中执行要分析的操作。采集期间，工具栏的 **Run** 按钮会变为 **Stop Capture**，深色蒙层会阻止操作数据面板。覆盖目标时段后点击 **Stop Capture**；连接中断时蒙层也会消失。

在 **Capture stopped** 对话框中选择 **Save and Symbolize** 或 **Later**。选择前者后，先指定与应用同一构建的符号库，再选择 `.loli` 保存位置，等待保存和符号化进度窗口关闭。GUI 会生成单独的 `.symbolized.loli` 文件并在成功后打开，原始 `.loli` 文件仍保留。选择 **Later** 后仍可在 GUI 中查看本次采集；需要时通过 **File > Save Record As**（Ctrl+S / Cmd+S）保存，再使用 **File > Symbolize Record**。

![采集停止后保存并符号化的对话框](images/imgui-capture-stopped.png)

时间线、调用栈树、矩形树图、smaps 和截图面板显示采集结果。采集时生成的截图会写入 `.loli` 文件，并保留在符号化副本中；符号化无法补回旧文件中缺失的截图。在调用栈面板的搜索箭头后，通过下拉框选择 **All Allocations** 或 **Persistent**；该选择同时作用于调用栈和矩形树图，只改变已保存数据的查看方式，不改变启动时的记录策略。在调用栈或矩形树图的搜索框输入函数名，按 Enter 跳到下一处，使用 `<` 和 `>` 前后跳转。

**Console** 标签页显示启动步骤、文件操作、错误和耗时；可通过 **Window > Console** 重新打开。右击日志文本可选 **Copy all** 或 **Clear view**；只有视图已在底部时才自动跟随新日志。要保存日志，可在 **File > Settings > Diagnostics** 勾选 **Write diagnostics to file** 并设置路径，或启动 GUI 时加上 `--log-file profiler.log`。仅用 `--log-file` 时，日志写到程序旁边的 `loli_gui.log`。默认不写日志文件。`--log-level debug` 可显示带源码位置的调试记录；GUI 和 CLI 使用同一套日志接口。

在时间线上拖拽选择一个区间后，调用栈和矩形树图只显示该区间内的分配；清除选择可恢复完整数据。选中区间后，点击工具栏的 **Leaks**，再选择 **Tree View** 或 **Treemap**，可查看两个时间点之间增长至少 1 KiB 的调用栈。选择 **Persistent** 时，此比较也会排除采集结束前已释放的分配。

![当前 ImGui 已保存记录视图：调用栈、矩形树图、时间线和截图](images/imgui-overview.png)

## 4. 符号与离线分析

符号文件必须与 APK 来自**同一次构建**。可离线重新翻译已保存的数据：

```text
LoliProfilerCLI --symbolize capture.loli --symbol libUE4.so --out capture-symbolized.loli
```

生成供 Python `agentcli` 使用的 SQLite 快照：

```text
LoliProfilerCLI --dump capture-symbolized.loli --out capture.db
python -m agentcli.cli summary capture.db
```

安装后仍可使用 `loli` 命令。更多选项见 [CLI 文档](CLI_MODE.md)，设备和符号问题见 [故障排查](TROUBLE_SHOOTING.md)。
