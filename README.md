# 串口值匹配器（SerialValueMatcher Native）

串口值匹配器是面向中文母语用户的 Windows 原生串口调试与数值分析工具。项目作者：`w`。

目标：先做一个成熟、稳定、中文优先、无需 C#/.NET Desktop Runtime 的串口调试工具，再在其上接入扩展拟合扫描、字段分析、协议规则保存与证据导出能力。

## 项目身份

- 中文名：串口值匹配器
- 英文/仓库名：SerialValueMatcher Native
- 作者：`w`
- 主要用户：中文母语的现场调试、设备联调、协议验证人员
- 交付目标：Windows 端原生可运行便携软件，解压后可直接启动，不以安装器为第一目标
- 技术边界：当前稳定基线为 C++20 + Qt6 原生桌面应用；架构级瘦身路线会新增 Win32 native 小包目标；两条路线都不依赖 C#/.NET Desktop Runtime

## 技术路线

- C++20
- Qt6 Widgets（当前稳定基线）
- Win32 native（架构级瘦身目标，分阶段落地）
- CMake / Ninja
- QSerialPort
- SQLite / QtSql
- QtTest

## 路线原则

- 不依赖 .NET Desktop Runtime，降低 Windows 用户安装负担。
- 不直接 fork GPL / 禁商用候选项目。
- 不从空白 Qt 窗口盲写，而是 clean-room 吸收候选工具审查结论：
  - SuperDT：多会话与连接抽象思想；
  - IKUN serial_debug_tool：DataPacket / PacketProcessor 数据管线思想；
  - RYCOM / CuteCom：串口参数、HEX 显示、发送历史、宏等功能细节；
  - Serial Studio / Vodka：数据管线、脚本解析、可视化/插件接口参考。
- 用户可见内容中文优先：界面、状态提示、错误诊断、验证报告和文档都优先按中文用户认知组织。
- Windows 端体积目标分两步推进：先保留 Qt baseline 可运行包，再建立不含 `Qt6*.dll` 的 Win32 native 小包；第一阶段目标 zip `<= 5 MB`、解压 `<= 8 MB`，冲刺目标 zip `<= 2 MB`、解压 `<= 3 MB`。

## 当前能力

当前工程已经不只是初始化骨架，已具备串口调试、Modbus RTU 扫描、候选值分析、协议规则保存与验证的最小闭环。

### 串口调试基础

- `SerialPortEnumerator`：枚举串口并提供端口描述模型。
- `SerialPortService`：打开/关闭串口，处理 RX/TX 原始事件。
- `SerialErrorTranslator`：把常见串口打开/运行错误转换为中文诊断，同时保留原始 Qt 错误信息。
- `SerialReconnectPolicy`：提供自动重连策略边界。
- 最小 Qt Widgets 中文 UI：
  - 刷新端口；
  - 波特率、数据位、校验位、停止位、流控、DTR、RTS；
  - 连接/断开；
  - 文本/HEX 发送；
  - 无/CR/LF/CRLF 行尾；
  - 发送历史；
  - RX/TX 事件显示；
  - 暂停滚动、清空接收区。

### 会话与数据持久化

- `SessionStore` 基于 SQLite / QtSql 保存：
  - 原始通信事件；
  - 发送历史；
  - 串口配置 Profile；
  - Modbus 扫描观测；
  - 候选匹配结果；
  - 稳定性分析结果；
  - 协议字段规则；
  - 规则验证记录。
- 数据库 schema 支持兼容升级，便于后续现场版本迭代。
- `SessionStore` 实现已按职责拆分为 schema、scan、matching/stability、rules 多个编译单元，公共 API 保持稳定。
- 扫描观测、匹配运行、稳定性分析、协议规则和规则验证相关读取会显式记录 `lastReadErrorText()`，避免把数据库读取失败误判为空数据。

### 协议与 Modbus RTU

- 校验函数：SUM8 / XOR8 / LRC8。
- `PayloadCodec`：文本/HEX 编码与行尾模型。
- Modbus RTU：
  - RTU 编解码；
  - 读请求构造；
  - 读响应解析；
  - 扫描计划；
  - 串口传输适配；
  - 扫描执行器；
  - 扫描观测持久化；
  - 主界面“Modbus 扫描”薄入口，可按当前串口参数发起 FC03/FC04 只读扫描并保存扫描会话。
- Modbus 扫描在 `ModbusScanWorker` 的工作线程中执行，`QtSerialByteChannel` 封装阻塞式 `QSerialPort` 打开、DTR/RTS 设置、写入完整性和超时/错误诊断，避免长扫描阻塞 UI。

### 值候选、稳定性与协议字段规则

- `ValueCandidateGenerator`：从寄存器观测中生成多种候选值。
- `NumericDecoder`：统一候选生成和规则验证使用的数值解码语义。
- `CandidateStabilityAnalyzer`：分析候选值稳定性。
- `StabilityAnalysisWorkflow`：从最近匹配运行聚合候选观测并保存稳定性分析结果，支撑 UI 中“执行多样本稳定性分析”入口。
- 候选生成使用有界 Top-N 工作集，规则验证会预索引最新观测样本，减少重复扫描和排序成本。
- 支持把稳定候选确认为协议字段规则，并保存字段名、地址、寄存器数量、倍率/偏移、单位、置信度、证据摘要等元数据。
- `ProtocolRuleVerifier`：根据扫描观测验证协议字段规则，输出：
  - 验证状态；
  - 解码值；
  - 工程值；
  - 原始寄存器；
  - 观测证据；
  - 中文状态说明。

### 规则解释与报告

- 已支持规则解释类型：
  - `BitFlags`：报警位 / 状态位解释；
  - `EnumMap`：枚举值解释。
- `protocol_rule_interpretation` 模块统一处理：
  - 解释映射解析；
  - 保存前校验；
  - 预览文本；
  - 验证时解释文本生成。
- UI 保存规则前会校验解释映射，减少位号越界、重复枚举值、缺少等号等现场输入错误。
- Markdown 验证报告可输出规则解释文本，便于现场交付和复盘。

## 当前测试基线

当前默认 CTest 基线：**33/33 passed**，其中包含 29 个 QtTest 目标、2 个 Qt-free native core 测试目标、1 个 Win32 串口后端硬件无关测试目标和 1 个 native storage 测试目标。

覆盖范围包括：

- Modbus RTU 编解码、读请求/读响应、扫描计划、扫描执行器、串口传输适配、扫描持久化；
- 数值解码、候选值生成、匹配持久化、稳定性分析、稳定性持久化；
- 最近匹配运行到稳定性分析结果的工作流；
- 协议规则持久化、规则验证、规则解释映射、验证记录持久化、验证报告；
- 校验函数、会话存储、控制台模型、发送编码、发送历史；
- 串口配置、端口选择、重连策略、串口错误中文诊断。
- 默认质量回归测试覆盖大样本候选生成、规则验证索引路径和稳定性分析分组路径。
- Qt-free native core 测试覆盖协议/Modbus 基础、数值解码、候选生成、稳定性分析、规则解释和 Markdown 报告渲染，为 Win32 native 小包路线提供不依赖 Qt 的核心验证。

常用验证命令：

```bash
bash scripts/check-env.sh
cmake -S . -B build-codex -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-codex --parallel 1
ctest --test-dir build-codex --output-on-failure
```

说明：`build-codex` 是本地接管验证目录；常规开发可继续使用自己的 `build` 目录。当前低内存 Debian VM 上优先使用 `--parallel 1`，比并行构建更稳。

可选压力测试默认不进入日常构建，可按需开启：

```bash
cmake -S . -B build-stress -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSVM_ENABLE_STRESS_TESTS=ON
cmake --build build-stress --target quality_stress_tests --parallel 1
ctest --test-dir build-stress --output-on-failure -L stress
```

## CI 与 Windows 便携包

仓库包含 Linux Qt 自动化检查：`.github/workflows/linux-qt.yml`。

CI 在 Ubuntu runner 上检出仓库，安装 Qt6 / CMake / Ninja 依赖，然后执行与本地一致的检查链路：

```bash
./scripts/check-env.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 1
ctest --test-dir build --output-on-failure
```

仓库还包含手动/定期触发的压力测试 workflow：

- `.github/workflows/linux-qt-stress.yml`：Linux 非硬件压力测试，使用 `-DSVM_ENABLE_STRESS_TESTS=ON` 构建并只运行 `stress` 标签测试。
- `.github/workflows/windows-qt-stress.yml`：Windows 2022 Release 非硬件压力测试，记录 stress CTest 运行耗时，用于补充 Windows 端资源/长期运行证据。

这些压力测试不影响普通 push / PR 检查链路。

仓库也包含 Windows 便携包 workflow：`.github/workflows/windows-qt-package.yml`。

该 workflow 在 `windows-2022` runner 上安装 Qt 6 x64 MSVC，执行 Release 构建、CTest 和 `scripts/package-windows.ps1`，并上传便携 zip artifact：

```text
SerialValueMatcherNative-win-x64
```

artifact 内包含：

```text
SerialValueMatcherNative-win-x64.zip
SerialValueMatcherNative-win-x64.zip.sha256.txt
SerialValueMatcherNative-win-x64.package-summary.txt
```

该 zip 是 Windows 原生便携运行包，不是安装器；目标是解压后直接启动 `svm-native.exe`。
Windows workflow 的关键第三方 action 固定到审核过的提交 SHA，避免移动 tag 在发布链路中漂移。
当前主发布 artifact 仍是 Qt baseline 包，允许携带 Qt 运行库。Win32 native preview artifact 已建立并通过体积门禁；最新 zip/解压/主程序大小以 artifact 内的 `SerialValueMatcherNative-win32-native-x64.package-summary.txt` 为准，包内不得包含 `Qt6*.dll`、`qsqlite.dll` 或 `sqldrivers`。Win32 native 已补入菜单栏、完整串口参数、发送历史、Profile、暂停滚动、自动重连、基础 Modbus 扫描、轻量候选生成、规则验证和 Markdown 报告导出；后续主发布切换仍取决于稳定性分析、规则编辑表格、证据浏览和真实 Windows 串口验收。

## 开发环境要求

Debian 12 可参考安装：

```bash
sudo apt install cmake ninja-build pkg-config g++ qt6-base-dev qt6-base-dev-tools qt6-serialport-dev libqt6sql6-sqlite
```

当前工程依赖 Qt6 Core / Widgets / SerialPort / Sql / Test，并需要 Qt SQLite SQL driver（QSQLITE 插件）。

可先运行环境检查：

```bash
./scripts/check-env.sh
```

## 构建

安装依赖并通过环境检查后：

```bash
bash scripts/check-env.sh
cmake -S . -B build-codex -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-codex --parallel 1
ctest --test-dir build-codex --output-on-failure
```

## Windows 打包

Windows 原生打包脚本已建立：

- Qt baseline 打包脚本：`scripts/package-windows.ps1`
- Win32 native 打包脚本：`scripts/package-windows-native.ps1`
- Win32 native 包检查脚本：`scripts/inspect-windows-package.ps1`
- 部署说明：`docs/windows-deployment.md`
- Qt baseline 自动出包：`.github/workflows/windows-qt-package.yml`
- Win32 native 自动出包：`.github/workflows/windows-native-package.yml`

典型命令：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1 -BuildDir build -Config Release
```

打包脚本会调用 `windeployqt`，并校验关键 Qt 插件：

- `platforms/qwindows.dll`
- `sqldrivers/qsqlite.dll`

打包脚本还会生成体积摘要，记录 zip bytes、解压后 bytes、文件数、最大文件和 Qt DLL 列表。Win32 native 小包的具体门禁见 `docs/windows-native-slimming.md`。

Win32 native shell 已作为默认关闭的并行目标接入：

```powershell
cmake -S . -B build-win32-native -G "Visual Studio 17 2022" -A x64 -DSVM_BUILD_QT_APP=OFF -DSVM_BUILD_QT_TESTS=OFF -DSVM_BUILD_WIN32_APP=ON
cmake --build build-win32-native --config Release --target svm-native-win32
.\build-win32-native\Release\svm-native-win32.exe --self-test
```

该目标用于瘦身迁移验证，当前尚未替代 Qt baseline 发布包。原因见 `docs/windows-native-parity.md`：native shell 已覆盖中文菜单、基础串口终端、Profile、发送历史、自动重连、基础 Modbus 扫描、轻量候选生成、规则验证和报告导出入口，但尚未覆盖完整稳定性分析、规则编辑表格和证据浏览 UI。

native 包命令：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-windows-native.ps1 -BuildDir build-win32-native -Config Release -SkipBuild
```

脚本会运行 `--self-test`、生成 zip/SHA256/体积摘要，并失败于 `Qt6*.dll`、`qsqlite.dll` 或第一阶段体积门禁超限。

实际发布包仍需在 Windows Qt 构建机上完成最终验收。

## 相关文档

- `native-spec.md`：原生版规格说明。
- `docs/architecture.md`：架构说明。
- `docs/windows-deployment.md`：Windows 打包与验收说明。
- `docs/windows-native-slimming.md`：Windows 架构级瘦身路线、体积基线和 native 小包门禁。
- `docs/windows-native-parity.md`：Win32 native 与 Qt baseline 的功能对照和发布切换决策。
- `docs/windows-serial-validation.md`：Win32 native 串口后端的真机验收清单。
- `docs/windows-native-ui-validation.md`：Win32 native UI shell 的自测和手工验收清单。
- `docs/task-*.md`：阶段性任务记录。
