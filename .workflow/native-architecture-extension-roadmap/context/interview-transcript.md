# Interview Transcript — Native Architecture Extension Roadmap

Started: 2026-07-05T17:28:00+08:00

This transcript records Phase 1 requirements answers. Source of truth is also mirrored in `state.json`.

---

### Q1: 请确认本轮总体目标边界是哪一种？
**A:** A：先架构整备，再接新功能。先处理布局模型生产化、主窗口 controller 边界、Modbus executor 统一、storage 拆分边界、CMake/version 工具化；确认稳定后再进入新功能开发。
**Category:** project_vision,functional_scope
**Timestamp:** 2026-07-05T18:45:23+08:00
---

### Q2: 本轮 Phase 4 实际整改的必做范围，你倾向哪一种？
**A:** ABCD 都得做。总体范围覆盖 UI/架构整备、后台能力整备、核心五项、异步写队列、会话证据包、命令序列基础、格式化/静态分析门禁等方向；后续需要通过分期控制风险。
**Category:** functional_scope,development_quality,edge_cases_risk
**Timestamp:** 2026-07-05T18:54:57+08:00
---

### Q3: 这些整改应该如何分期？
**A:** A：三期稳态交付。一期 UI/架构地基，二期后台一致性，三期扩展能力、质量门禁和发布门禁增强。
**Category:** development_quality,edge_cases_risk
**Timestamp:** 2026-07-05T19:08:30+08:00
---

### Q4: 本轮架构整备应服务的核心用户旅程，是否按下面这个口径？
**A:** A：单人本地开发测试工程师旅程。用户在 Windows 本地打开 GitHub Actions 编译的 exe，连接串口设备，进行收发、日志观察、Modbus 扫描、候选值分析、规则验证、导出证据/报告。不做账号、多用户协作、云同步、权限体系。
**Category:** user_personas,ux_design,security_compliance
**Timestamp:** 2026-07-05T19:10:51+08:00
---

### Q5: 数据模型和持久化边界建议按哪个口径？
**A:** 倾向 D，引入数据库；但不确定是否必要。后期希望加入多种协议，同时仍希望软件保持很小体量。
**Category:** domain_data_model,tech_stack,edge_cases_risk
**Timestamp:** 2026-07-05T19:16:33+08:00
---

### Q6: 是否确认采用这个存储路线？
**A:** E：兼容文件存储 + SQLite 决策门。短期保持现有 native 文件存储兼容，抽象 `SessionStore` 窄接口、补事务/恢复、设计 schema version；架构预留 SQLite backend，只有多协议数据、证据包、命令序列、跨会话查询确实需要索引和关联查询时才引入 SQLite。
**Category:** domain_data_model,tech_stack,edge_cases_risk
**Timestamp:** 2026-07-05T19:18:52+08:00
---

### Q7: 技术栈和依赖治理是否采用 A2：10MB 预算下的保守 native 主线？
**A:** 认可 A2，但 10MB 是绝对上限/红线，不是目标体量。不能因为设置了 10MB 就放宽当前编码和架构要求；当前小体量基线应继续保持，新增依赖和模块必须证明必要性、包体增量和架构收益。
**Category:** tech_stack,performance_scalability,development_quality,observability_operations
**Timestamp:** 2026-07-05T19:28:06+08:00
---

### Q8: 安全与合规边界是否按下面口径？
**A:** A：本地工具最小攻击面。不联网、不遥测、不上传数据；不做账号/权限/云同步；默认普通用户权限运行，不要求管理员；只访问用户选择的串口、文件和本地存储目录；导出报告不包含隐私路径或可选脱敏；危险写入/批量命令/广播 Modbus 写入后续必须有显式确认；CI 检查包体来源、依赖、签名/哈希和 forbidden runtime。
**Category:** security_compliance,edge_cases_risk,observability_operations
**Timestamp:** 2026-07-05T19:35:05+08:00
---

### Q9: 未来 TCP/多协议边界怎么定？
**A:** A：本轮不实现 TCP，但架构预留 transport 抽象。当前 Phase 4 不做 TCP UI 和 TCP 功能；架构不写死串口，预留 `SerialTransport` / `TcpClientTransport` / `ProtocolParser` 等边界。未来做 Modbus TCP 或局域网 TCP 时，再单独开安全边界和验收门禁。
**Category:** integration,security_compliance,edge_cases_risk,tech_stack
**Timestamp:** 2026-07-06T08:14:13+08:00
---
