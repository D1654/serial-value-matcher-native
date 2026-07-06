# Draft Cache — Native Architecture Extension Roadmap

Internal session-resume cache for Phase 2. This is not a deliverable.

Updated: 2026-07-06T16:21:45+08:00

## Completed Sections

1, 2, 3, 4, 5, 6, 7, 8, 9

---

## Section 1 — Project Overview

SerialValueMatcher Native 当前是一个面向本地 Windows 开发测试工程师的轻量 Win32 native 串口 / Modbus / 数值匹配 / 证据报告工具。v1.0.4 已形成可用稳定基线，本轮目标不是盲目加功能，而是在新功能进入前建立长期可维护、可测试、可扩展且仍保持小体量的架构跑道。

核心问题是：如果继续把 UI、串口、Modbus、存储、报告和发布门禁堆进主窗口或重复路径，后续新增多协议、命令序列、证据包和更强分析能力时会反复引发 UI 回归、卡顿、行为分叉和维护成本上升。解决方式是先完成分层边界、布局模型生产化、后台一致性、存储窄接口和质量门禁，再进入新的功能开发。

目标用户是中文母语的单人本地开发测试工程师：打开 GitHub Actions 编译的 exe，连接串口设备，完成收发、日志观察、Modbus 扫描、候选值分析、规则验证和本地证据/报告导出；不做账号、多用户协作、云同步、遥测或权限体系。

## Section 2 — Architecture Design

Brainstorm basis: BS-2 chose a layered native architecture with high confidence.

### High-Level Pattern

采用“薄 Win32 Shell + 功能 Controller + Core Service + Ports/Adapters + 生产级验证门禁”的分层 native 架构。它不是完整插件框架，也不是重 UI 框架迁移；它的目标是把当前真实风险点分解清楚：主窗口膨胀、生产布局漂移、Modbus 路径分叉、同步串口写卡顿、存储一致性和发布元数据漂移。

### Layer Decomposition

- Presentation Shell: `NativeMainWindow` 负责 Win32 消息路由、生命周期、HWND 宿主、菜单/命令分发和帧调度。
- UI Model/Layout: `NativeLayoutModel` 成为生产布局计算来源；布局事务负责成批移动/显示控件，降低 resize 闪烁和测试/真实布局漂移。
- Feature Controllers: serial, send, log, workbench, Modbus, analysis, preferences 等功能拥有自己的状态、命令和 UI 绑定边界。
- Core Services: 协议解析、Modbus executor、数值匹配、会话证据、报告生成、命令序列执行基础。
- Ports/Adapters: `SerialTransport` 当前落地；`TcpClientTransport` / protocol parser 边界仅预留，Phase 4 不实现 TCP UI 或 TCP runtime。
- Storage Port: 窄 `SessionStore` 接口，现有文件 backend 继续兼容；补事务/恢复/schema version；SQLite backend 保持决策门。
- Quality Architecture: CTest、本地 self-test、Wine/Xvfb UI 检查、串口仿真压力、包体/依赖/version/release gate 成为架构一部分。

### Component Diagram

```text
Win32 Message Loop
        |
        v
+--------------------+
| NativeMainWindow   |  message/lifetime/HWND shell
+---------+----------+
          |
          v
+--------------------+      +----------------------+
| Feature Controllers|----->| NativeLayoutModel    |
| serial/send/log/...|      | Layout Transactions  |
+---------+----------+      +----------------------+
          |
          v
+--------------------+      +----------------------+
| Core Services      |----->| SessionStore Port    |
| protocol/modbus/   |      | file backend now     |
| matcher/report     |      | SQLite gate later    |
+---------+----------+      +----------------------+
          |
          v
+--------------------+
| Transport Port     |
| Serial now         |
| TCP reserved later |
+--------------------+
```

### Key Design Patterns

- Ports and Adapters: transport/storage/protocol backend 可替换，但不提前实现无需求的 runtime。
- Controller / Command: UI 命令进入 feature controller，再进入 service 或 transport，避免主窗口直接承载业务。
- Repository-like Storage Port: storage 使用窄接口和事务边界，避免大 facade 继续扩张。
- Event Queue / Batched UI Updates: 后台 I/O 和长任务通过事件/批处理回到 UI，避免 UI 线程阻塞和 resize/日志刷新抖动。
- Model-Driven Layout: 所有生产布局先算模型再提交到 HWND，避免局部控件手工位置计算。

### Data Flow

用户操作从菜单、按钮或标签页进入 `NativeMainWindow`，由命令路由给对应 feature controller。controller 校验状态并调用 core service 或 transport port。串口/后台任务产生的 RX/TX、错误、扫描结果、匹配结果以事件形式进入日志、状态模型、存储和报告服务。UI 只消费稳定状态和布局模型，不直接同步等待串口写入或跨模块读取内部数据。

### Decision Rationale

BS-2 的关键结论是：Win32 官方模式支持 per-window state 和消息分发，但不支持把所有业务放在 `WndProc` / 主窗口；DeepWiki 对 Windows samples 的验证也支持 resize/paint/background work 分离。由于用户要求小体量和本地高性能，本轮不引入重框架或通用插件系统，而是针对当前热区做必要边界。

## Section 3 — Tech Stack Selection

Brainstorm basis: BS-3 chose a conservative native stack with high confidence.

### Language and Runtime

C++20 + Win32 API 继续作为生产交付主线。理由是当前 release 已由用户验证体验良好，Win32 native 能保持小体量、启动快、运行时依赖可控，并直接覆盖串口、窗口、DPI、消息调度和本地文件访问需求。

### Serial and Protocol Foundation

串口层使用 Win32 communications API 直接实现，围绕 `CreateFile`、`DCB`、`COMMTIMEOUTS`、`ReadFile`、`WriteFile` 和 overlapped/background I/O 建立稳定边界。Qt SerialPort 只作为 legacy baseline 和行为参考，不回流 native release runtime。

### UI Framework

继续 Win32 native，不迁移 Qt、Electron、WebView 或重 dashboard。UI 质量不靠框架替换解决，而靠生产化 `NativeLayoutModel`、controller 边界、批量布局提交、日志刷新节流、DPI/resize 验证和截图/UI perf 门禁解决。

### Storage

短期继续兼容现有 native file storage；本轮重点是窄 `SessionStore`、事务/恢复、schema version 和证据模型边界。SQLite 作为未来 backend 决策门，仅在多协议数据、证据包、命令序列或跨会话查询真正需要索引和关系查询时引入。

### Build, Test, and Release Toolchain

CMake + CTest + GitHub Actions 是主干。Wine/Xvfb UI 检查、串口仿真压力、self-test、包体大小、runtime 依赖、版本元数据、文档/README/release 一致性都要进入后续计划。GoogleTest 可作为开发/CI-only 测试框架引入，用于纯逻辑、layout model、parser、matcher、storage transaction 和 fake transport；不得进入 release runtime。

### Dependency Governance

本轮不引入 vcpkg release 链路。只有当真实第三方依赖增长到手工治理会带来更大风险时，才采用 vcpkg manifest/baseline/triplet 策略。10MB 是绝对红线，不是目标体量；当前小体量基线应继续作为优化标准，新依赖必须说明必要性、包体增量和架构收益。

### Deployment

交付目标仍是 GitHub Actions 编译出的 Windows native exe/zip。Phase 4 不实现 TCP UI、TCP Client、TCP Server 或联网能力；transport/protocol 抽象只用于防止架构写死串口，为未来单独立项的 TCP/多协议开发保留边界。

## Section 4 — Algorithm & Design Strategy

Brainstorm basis: BS-4 chose a small event-driven algorithm strategy with high confidence.

### Performance-Critical Paths

本项目的核心性能路径不是单一算法，而是多个实时路径叠加：串口 RX/TX、日志追加与显示、窗口 resize/layout、Modbus 扫描事务、候选值匹配、会话存储和报告证据收集。策略是把这些路径拆成小而可测试的状态机和队列，避免 UI 线程承担 I/O 等待、解析、批量日志刷新或存储事务。

### Serial Send/Receive Strategy

串口读写采用后台 I/O 和事件回传模型。发送侧引入有界异步写队列，所有手动单发、批量发送和未来命令序列都走同一条路径；每个写请求必须有 accepted、sent、failed、timeout、cancelled 等结果事件。接收侧保留原始 RX 事件，再分发给日志、协议解析、匹配和证据服务。

关键要求：

- UI 线程不得同步等待串口写入完成。
- 队列需要 backpressure，避免高频发送或脚本化命令撑爆内存。
- 每条命令需要超时、取消和错误归因。
- 手动单发仍要即时反馈，不能因为异步化让用户失去确定感。

### UI/Layout/Log Strategy

布局和日志都采用“模型先行、批量提交”的策略。`NativeLayoutModel` 负责计算控件位置和可见性，布局事务负责一次性提交 HWND 调整；日志显示使用批量 flush 和节流刷新，但原始 TX/RX 证据先进入内存/存储事件，不以 UI 刷新作为数据可靠性的前提。

重点是减少 resize 时的重入、重复 invalidation、背景擦除和控件逐个闪烁，同时保留当前工具型密集 UI 的可扫描性。

### Modbus Transaction Strategy

Modbus 扫描、读写、候选寄存器匹配和规则验证统一进入一个 executor / transaction state machine。它负责请求构造、发送、等待响应、CRC/长度/功能码校验、异常码处理、超时、重试和结果归一化。native worker 只负责线程调度和 transport 适配，不再拥有独立协议解释。

这样做可以避免 UI 扫描路径、native worker 路径和 core/Qt legacy 路径分别实现超时、重试和异常处理，后续 Modbus TCP 或其他协议也能复用事务边界。

### Matching, Evidence, and Report Strategy

候选值匹配应保持为纯 core service：输入是原始帧、解析寄存器、用户已知值、数据类型、端序、倍率和规则；输出是候选列表、置信理由、验证步骤和证据片段。报告生成不应从 UI 控件读取结果，而应从 session/evidence model 读取结构化数据。

### Storage Strategy

存储短期仍使用兼容文件 backend，但必须补上 schema version、事务边界和 orphan recovery。对于多文件 append，至少要有写入顺序、commit marker 或等效恢复标记；异常退出后能扫描并丢弃/隔离未完成记录，避免后续证据包建立在不一致状态上。

### Automation Strategy

自动化从声明式命令序列和断言开始，不引入完整脚本引擎。命令序列复用异步写队列、Modbus executor、日志证据和危险操作确认机制；广播写入、批量写入、危险寄存器写入必须显式确认并进入操作证据。

### Testing Strategy

算法测试优先覆盖：

- layout model resize/DPI/极小窗口边界。
- parser/matcher 数据类型、端序、倍率和异常输入。
- fake serial transport 下的 RX/TX、超时、取消、失败和 backpressure。
- Modbus executor 的正常响应、异常响应、CRC/长度错误、设备无响应和重试。
- storage transaction 的异常退出、半写入、orphan recovery 和 schema version。
- UI perf/self-test 下的 resize、日志高频刷新和长时间运行。

## Section 5 — Production Architecture

Brainstorm basis: BS-8 chose a desktop-native production architecture with high confidence.

### Deployment Architecture

本项目的生产交付形态是 GitHub Actions 编译出的 Windows native exe/zip，不是云服务。因此不引入 Docker、Kubernetes、服务发现、服务网格或服务器自动扩缩容。生产架构核心是：可复现构建、可验证 artifact、可审计 release、可诊断运行、可恢复本地状态。

```text
Developer Commit
      |
      v
GitHub Actions
  |-- configure/build
  |-- CTest/self-test
  |-- serial simulation / fake transport
  |-- Wine/Xvfb UI screenshot + perf
  |-- package content / size / dependency audit
  |-- version/docs/release consistency
      |
      v
Windows exe/zip + logs + screenshots + audit reports
      |
      v
GitHub Release asset
      |
      v
User runs local Windows tool
  |-- local logs
  |-- local session/evidence storage
  |-- optional diagnostic export
```

### Observability Design

本地桌面软件的 observability 不是远程监控，而是本地诊断和 CI 证据：

- Runtime logs: TX/RX、串口状态、错误、Modbus 事务、存储恢复、危险操作确认、性能计数摘要。
- Self-test logs: 启动自检、UI perf、串口仿真、storage recovery、version metadata。
- Release evidence: Actions artifact 中保留 exe/zip、测试日志、UI 截图、包体审计、依赖审计、hash/版本信息。
- Diagnostic bundle: 用户主动导出，默认本地保存，可选脱敏本地路径、设备标识和原始业务数据。
- Health model: 不做后台服务 health endpoint；改为应用内自检状态和菜单/日志可见的诊断结果。

### Security Hardening

- 默认不联网、不遥测、不上传、不做账号/权限/云同步。
- 普通用户权限运行，不要求管理员。
- 只访问用户选择的串口、文件、导出路径和本地配置/会话目录。
- Phase 4 不实现 TCP Client、TCP Server、监听端口或 TCP UI。
- 危险写入、批量命令、广播 Modbus 写入必须显式确认并写入会话证据。
- 报告和诊断导出默认避免泄露绝对路径，支持脱敏。
- Release gate 检查 forbidden runtime、异常 DLL、包体大小、version metadata 和文档一致性。
- 代码签名作为后续 release hardening gate；没有证书前不阻塞架构整备。

### Data Protection

本项目数据主要是本地会话、串口原始帧、用户操作、扫描参数、匹配结果和报告。保护策略：

- 存储 schema version 明确，便于后续迁移。
- 文件 backend 增加事务/commit marker/orphan recovery。
- 异常退出后启动时执行恢复扫描，并在日志中提示恢复结果。
- 导出路径由用户选择，不自动上传。
- 诊断包和报告区分“完整证据”和“脱敏摘要”。
- 不在配置文件中保存不必要的隐私路径或设备现场数据。

### Scaling & Resilience

这里的 scaling 是本地负载能力，不是服务器横向扩容：

- 高波特率/长会话：有界 RX/log buffer、批量 flush、背压和内存上限。
- 高频发送/命令序列：异步写队列、超时、取消和队列长度限制。
- Modbus 扫描：统一事务状态机、重试/超时策略、设备无响应隔离。
- UI resize/log 刷新：布局事务、重绘最小化、UI perf gate。
- 串口断开/占用：明确错误归因、可恢复状态、不会卡死 UI。
- 存储异常：事务恢复、失败提示、不会破坏已有已提交会话。

### CI/CD Pipeline

推荐流水线顺序：

1. Configure/build: CMake 生成、编译 native exe。
2. Unit/core tests: layout model、parser/matcher、storage、transport fake。
3. Native self-test: 启动、版本、菜单、关键后台路径。
4. UI verification: Wine/Xvfb 截图、关键标签页、resize、UI perf。
5. Serial simulation/stress: CI 中先使用可运行的 fake transport / native tests；当前 PTY loopback matrix 仍属于 local pre-release evidence，直到能在 CI 中稳定阻塞执行。
6. Package audit: exe/zip 大小、依赖、forbidden runtime、文档目录、README/release 一致性。
7. Release evidence: 上传日志、截图、审计报告、hash、版本信息。
8. Release publish: 仅通过所有门禁后生成/更新 GitHub Release。

### Runbooks & Operations

需要维护短 runbooks，而不是口头经验：

- 本地构建与测试。
- GitHub Actions artifact 下载与验证。
- Release 发布/替换/回滚。
- UI 截图审查。
- 串口仿真压力测试；当前 PTY loopback 为 local pre-release evidence，Phase 3 需决定是否补齐 CI 可阻塞替代方案。
- 包体大小和依赖异常处理。
- 用户反馈诊断包审查。
- 崩溃/启动失败/会话恢复失败处理。

### Infrastructure as Code / Configuration

本项目不需要云 IaC。等价的生产配置是 workflow-as-code：

- GitHub Actions workflow 固化 build/test/package/release gates。
- CMake/CTest 固化 target、test、install/package 边界。
- scripts 固化 UI capture、serial stress、package audit、docs consistency。
- 版本来源单一化，避免 README、release、resource metadata、包名漂移。

### Production Readiness Exit Criteria

Phase 4 每个阶段完成时至少满足：

- 本地 CTest 通过。
- GitHub Actions Windows artifact 通过。
- UI 截图无空白/盲点/错位，resize 通过。
- UI perf 以当前 release/artifact 的 `--ui-perf-test` 输出建立基线，不允许无解释回退。
- 串口 fake/loopback 压力具备可复现证据；PTY loopback 在 CI 不支持前必须作为本地 release-candidate 证据保留。
- 包体仍显著低于 10MB 红线，且无新增未解释 runtime。
- README、doc、release 说明与实际包一致。
- 每个新增能力都有可复现测试或明确手工验收记录。

## Section 6 — Project Structure

### Proposed Structure

本轮不建议先做大规模目录搬迁。更稳妥的方式是保留现有目录，逐步把职责边界收窄；只有当接口稳定、测试覆盖到位后，再做低风险文件移动。

```text
serial-value-matcher-native/
├── src/
│   ├── win32/                 # Win32 shell, HWND binding, native UI state/layout
│   │   ├── main_window*       # 短期保留，但逐步瘦身为 shell/host
│   │   ├── native_*_state*    # UI state/layout/policy/scheduler
│   │   └── controllers/       # 可选目标方向：仅当行为保护下的 controller 抽取确实需要时创建
│   ├── core/                  # 不依赖 HWND 的核心协议/分析/报告基础
│   ├── protocol/              # checksum/payload codec/parser utilities
│   ├── modbus/                # Modbus RTU codec, request/response, scan executor
│   ├── transport/             # serial transport ports/adapters; TCP 仅预留接口，不落 runtime
│   ├── capture/               # raw TX/RX event stream and evidence bus
│   ├── matching/              # numeric decoder, candidate generator, rule verifier
│   ├── storage/               # storage domain records and narrow interfaces
│   ├── native_storage/        # compatible native file backend
│   ├── report/                # report generation, writers
│   ├── analysis/              # stability/candidate workflows
│   └── session/               # local session/console model
├── tests/                     # CTest targets; future optional GoogleTest stays dev/CI-only
├── scripts/                   # packaging, UI capture, serial stress, docs/package audits
├── docs/                      # Chinese-first user/dev/release docs and screenshots
├── cmake/                     # toolchains, future version/package helpers
├── .github/workflows/         # build/test/package/release evidence workflow
└── .workflow/                 # workflow-architect process artifacts, not release content
```

### Module Organization Rationale

- `src/win32` 保留 Win32 细节，但业务状态、命令和后台行为逐步转给 controllers/services。
- `src/modbus` 成为唯一 Modbus 行为来源，native worker 只做线程/transport 适配。
- `src/transport` 先收敛串口 port/adapters，TCP 只保留命名边界，不实现功能。
- `src/storage` 定义数据模型和窄接口；`src/native_storage` 只负责文件 backend。
- `src/capture` / `src/report` / `src/analysis` 作为证据包和报告闭环的基础。
- `tests` 与 release package 明确隔离，测试可变强，但不得进入发布包。

### Configuration Management

- 应用配置：本地用户配置/会话配置，带 schema version 和兼容迁移。
- CI 配置：GitHub Actions workflow + CMake options + scripts，作为 workflow-as-code。
- Feature flags：仅允许编译期或本地诊断开关；不做远程配置、不做云端开关。
- Release metadata：版本号、产品名、resource metadata、zip/exe 名称和 README/release 文案必须单源化或由脚本一致性检查。

### Environment Handling

本项目没有 Web 意义上的 dev/staging/prod 环境，替代定义如下：

- Local Dev：开发机 CMake/CTest，本地 Wine 或 Windows 手工验证。
- CI Verification：GitHub Actions 编译、测试、截图、串口仿真、包体审计。
- Release Candidate：从 CI artifact 下载的候选 exe/zip，执行完整验收清单。
- Release：GitHub Release asset，配套 hash、说明、文档和证据。

## Section 7 — Implementation Phases

本轮执行建议维持用户已确认的三期稳态交付，每期完成后做 milestone checkpoint，不连续吞下大改动。

### Phase 1 — UI / Architecture Foundation

Objective: 先稳住 UI 和主窗口边界，为后续后台能力提供可验证地基。

Estimated tasks: 7-9

Scope:

- 生产 UI 全面消费 `NativeLayoutModel` / layout transaction。
- 收紧 `NativeMainWindow`：消息/lifetime/HWND shell 与 feature controller 边界分开。
- 建立 serial/send/log/workbench/modbus/analysis/preference controller 的最小可用边界。
- 统一 resize、DPI、tab、日志栏/标签栏布局规则和 UI perf gate。
- 以现有 `--ui-perf-test` 和 UI capture artifact 建立可比较基线，而不是只写“无明显回退”。
- 保持 v1.0.4 可用体验，不做 TCP 或大功能新增。

Dependencies: 无前置，是所有后续阶段前置。

### Phase 2 — Backend Consistency

Objective: 统一串口、Modbus、存储和后台任务行为，消除功能路径分叉。

Estimated tasks: 7-9

Scope:

- 引入有界异步串口写队列，覆盖单发、批量发送和未来命令序列。
- 将 native Modbus 扫描收敛到统一 executor / transport transaction。
- storage 从大 facade 继续拆分为窄接口和文件 backend。
- 补 schema version、commit/recovery、orphan recovery。
- 增强 fake serial、loopback、timeout/cancel、长日志和压力测试。
- 明确区分 CI-blocking fake/native tests 与当前 local-only PTY loopback evidence；能闭环则补齐 CI 替代方案，不能则写入 release-candidate runbook。

Dependencies: 依赖 Phase 1 的 controller/layout 边界。

### Phase 3 — Extension Capability & Production Hardening

Objective: 在稳定架构上加入扩展基础和发布级门禁，准备后续新功能开发。

Estimated tasks: 8-10

Scope:

- 会话证据包基础：TX/RX、用户操作、扫描参数、匹配结果、版本/SHA。
- 声明式命令序列 + 断言基础，不引入完整脚本引擎。
- 诊断包导出和可选脱敏。
- CMake/version 单源化、包体/依赖/forbidden-runtime gate。
- 明确 version metadata 单源任务：CMake project version、Win32 VERSIONINFO、package name、README、docs、release notes、artifact summary 必须一致。
- README/docs/release artifact 一致性 gate。
- UI screenshot/perf、serial stress、package audit 进入 release 必过清单。
- 评估格式化/静态分析门禁，不让工具进入 release 包。

Dependencies: 依赖 Phase 1-2。完成后再进入新的协议/TCP/高级功能立项。

## Section 8 — Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| `NativeMainWindow` 瘦身变成大规模重写，破坏已验证 UI | Medium | High | 按功能边界小步抽取；每步保留截图/UI perf/CTest；不做一次性搬迁 |
| 过度抽象导致代码更难读 | Medium | High | 每个接口必须对应现有痛点：布局、串口、Modbus、存储、证据；禁止泛插件框架 |
| 异步写队列引入顺序、取消、超时竞态 | Medium | High | fake transport + loopback + 压力测试；明确 accepted/sent/failed/timeout/cancelled 状态 |
| Modbus executor 统一时出现行为回归 | Medium | High | 用现有 Modbus tests 锁定 request/response/exception/CRC/timeout；先适配再替换 UI 路径 |
| 文件存储事务设计不足，证据包建立在不一致数据上 | Medium | High | Phase 2 优先补 schema version、commit marker、orphan recovery 和故障注入测试 |
| UI resize/log perf 在高性能电脑仍可见闪烁 | Medium | Medium-High | layout transaction、重绘最小化、批量 flush、UI perf gate 和截图复核 |
| Release artifact 与本地测试对象不一致 | Medium | High | 以 GitHub Actions exe/zip 为最终验收目标；下载 artifact 做 UI/功能/包体审计 |
| 串口 PTY 压力当前不是 GitHub Actions 阻塞门禁 | Medium | Medium-High | Phase 3 明确 local pre-release evidence 与 CI-blocking fake/native tests 的边界，并评估 CI 可运行替代方案 |
| 依赖治理被 10MB 红线误解为“可随便加到 10MB” | Low-Medium | Medium | 10MB 作为绝对红线；每个依赖必须证明必要性、增量和收益 |
| 未来 TCP/多协议需求提前污染当前安全边界 | Medium | High | 本轮只保留 transport 抽象，不实现 TCP UI/runtime；未来单独安全门禁 |
| 文档/release/readme 再次漂移 | Medium | Medium | docs/package consistency 脚本进入 release gate；截图和中文文档同步更新 |

Known unknowns:

- 是否以及何时引入 SQLite：由跨会话查询、证据模型复杂度、索引需求决定。
- 是否引入 GoogleTest：由纯逻辑/状态机测试复杂度决定，且仅限 dev/CI。
- 是否需要代码签名：由证书和发布策略决定，不阻塞架构整备。
- 后续多协议范围：TCP、HID、CAN 网关、模拟器都需单独需求阶段。

## Section 9 — Complexity Estimate

Overall complexity: Complex, but controllable.

原因不是单点技术难，而是多个已验证用户体验、Win32 UI、串口实时性、Modbus 行为、存储一致性、release 证据链同时被纳入质量目标。若按三期推进并保持每期验收，复杂度可控；若一次性重写或同时加多种协议，会升级为 Very Complex。

Estimated implementation phases: 3

Estimated total tasks: 22-28

Suggested task distribution:

- Phase 1 UI / Architecture Foundation: 7-9 tasks
- Phase 2 Backend Consistency: 7-9 tasks
- Phase 3 Extension Capability & Production Hardening: 8-10 tasks

Expected verification load:

- 每个任务：本地 targeted tests。
- 每期：完整 CTest + UI screenshot/perf + serial/fake transport checks。
- 每个 release candidate：GitHub Actions artifact 下载审查、包体/依赖/文档/release 一致性检查。

Delivery stance: 不以“功能数量”衡量成功，而以“新增功能可以按稳定边界进入，并且不会重新制造 UI 卡顿、标签页空白、串口阻塞、Modbus 分叉、文档漂移和 release 不可信”作为 Phase 4 成功标准。
