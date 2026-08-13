# openbrep-addon / AGENTS.md

## 接手任务前必读（Session Handoff）

**在开始任何开发工作前，先查阅以下内容，不要只靠 git log 推断背景：**

1. **最新 handoff 文档**（了解上次停在哪、未完成事项）：
   - 目录：`/Users/ren/Library/Mobile Documents/iCloud~md~obsidian/Documents/库/01-Projects/dev开发/OpenBrep 开发/addon开发/`
   - 找最新的 `handoff-YYYY-MM-DD.md` 文件阅读（同日多轮带序号：`-2`、`-3`）
2. **当前架构/任务单文档**：同目录的主题文档
   （如 `Copilot深度整合-架构与任务总单-2026-08-13.md`，正在进行的迁移以此为准）
3. **再看代码**。commit message 只是摘要，Obsidian 文档才有决策背景。

## 每轮工作结束必写 Handoff

每次开发会话结束（无论任务完成与否），在上述 `addon开发/` 目录写
`handoff-YYYY-MM-DD.md`（同日多轮追加序号），固定四节：

- **本轮完成**：做了什么，对应 commit 哈希
- **未完成/阻塞**：卡在哪、需要什么外部条件（如实机验证、DevKit 环境）
- **下次接续入口**：从哪个任务/文件继续，第一步做什么
- **验证状态**：编译 / 测试 / Archicad 实机分别是否通过

### 任务单施工模式（协作协议优先）

当本会话是按上述 `addon开发/` 目录的任务单施工时，以任务单的「协作协议」为准：
禁止 git / deploy 操作，完成后按任务卡格式输出完成报告，handoff 由指挥侧
验收后统一维护；本文「开发原则」中「每次任务结尾 commit + push + deploy」
的习惯在该模式下暂停适用。

## 项目结构

- 这是 Archicad 29 的 C++ Add-on，内嵌 AI GDL 修复助手
- C++ 部分：`Sources/` 目录，负责 Archicad 面板 UI 和生命周期
- Python 后端：`copilot/` 目录，FastAPI 服务跑在 `localhost:8502`
- 后端复用 gdl-agent 的 LLMAdapter 和配置：`/Users/ren/MAC工作/工作/code/开源项目/gdl-agent`

## 关键路径

- Python 环境：`/Users/ren/miniconda3/bin/python`（conda base），禁止用 `/usr/bin/python3`
- Archicad Add-Ons 目录：`/Applications/GRAPHISOFT/Archicad 29/Add-Ons/`
- 版本号：`Sources/AddOnVersion.hpp` 的 `ADDON_VERSION` 宏
- 参考项目：`/Users/ren/tapir-archicad-automation`

## 构建与部署

- 唯一部署命令：`bash deploy.sh`（从项目根目录）
- `deploy.sh` 流程：`cmake --build build` → rm/cp bundle 到 AC29 → zip 打包 → `gh release create`
- 禁止 `cd build` 再 `make`，会导致路径变成 `build/build/...`

## 后端启动

- 正确启动：`cd ~/MAC工作/工作/code/开源项目/openbrep-addon && python -m uvicorn copilot.server:app --port 8502`
- C++ 侧会在面板打开时自动启动后端（`CopilotPalette.cpp`）
- 端口占用时：`lsof -ti:8502 | xargs kill -9`

## LLM 配置

- 配置文件：`/Users/ren/MAC工作/工作/code/开源项目/gdl-agent/config.toml`
- `model` 字段必须填具体模型名（如 `gpt-5.2-codex`），不能填 provider 别名
- `custom_providers` 支持自定义 OpenAI 兼容代理

## 开发原则

- 每次任务结尾：`git add -A && git commit && git push && bash deploy.sh`
- 复杂功能先拆最小步骤验证，不要一步到位
- 遇到 Archicad API 用法不确定，参考 `/Users/ren/tapir-archicad-automation`

## 常见故障排查

- 后端未启动/面板空白：
  - 检查日志：`/tmp/copilot_debug.log`、`/tmp/copilot.log`
  - 检查启动脚本：`ls -la /tmp/start_copilot.sh`（若不存在，面板会自动重建）
  - 手动拉起：`cd ~/MAC工作/工作/code/开源项目/openbrep-addon && /Users/ren/miniconda3/bin/python -m uvicorn copilot.server:app --port 8502`
- 端口占用：
  - `lsof -ti:8502 | xargs kill -9`
- 面板反复无法启动（需重启Archicad）：
  - 可能原因：`/tmp/`目录被系统清理，启动脚本丢失
  - 解决方案：v0.3.2+版本已内置脚本重建逻辑，面板打开时会自动检查并重建
  - 若仍失败：检查`/tmp/copilot_debug.log`中`CreateStartScriptIfNeeded`日志
- UI 报“请求失败”：
  - 确认 `copilot/server.py` 正常运行，且 `gdl-agent/config.toml` 可读

## 构建依赖说明

- Archicad DevKit：`/Users/ren/MAC工作/工作/code/开源项目/API.Development.Kit.MAC.29.3100`
- Python 环境：`/Users/ren/miniconda3/bin/python`
- 构建命令（唯一入口）：`bash deploy.sh`
- 禁止 `cd build` 再 `make`，会产生 `build/build/...`

## 运行时配置清单

- 环境变量：
  - `GDL_AGENT_CONFIG`（可选）覆盖默认配置路径
- 默认配置文件：`/Users/ren/MAC工作/工作/code/开源项目/gdl-agent/config.toml`
- 配置示例（节选）：
  ```toml
  [llm]
  model = "gpt-5.2-codex"
  api_key = "YOUR_API_KEY"
  # api_base = "https://your-openai-compatible-endpoint/v1"

  [[llm.custom_providers]]
  name = "ymg"
  protocol = "openai"
  base_url = "https://example.com/v1"
  api_key = "YOUR_PROXY_KEY"
  models = ["ymg"]
  ```

## 发布流程说明

- 版本号更新：修改 `Sources/AddOnVersion.hpp` 中 `ADDON_VERSION`
- 发布命令：`bash deploy.sh`
- Release 命名规则：`v${ADDON_VERSION}`（脚本自动创建）
- 回滚流程：
  1. `git revert <bad_commit>`
  2. `git push`
  3. `bash deploy.sh`

## 代码修改边界

- 资源 ID 与菜单项：`RFIX/OpenBrepFix.grc`、`RINT/OpenBrep.grc` 变更必须先确认影响（菜单/本地化）
- 面板与浏览器加载逻辑：`Sources/CopilotPalette.cpp` 修改需先说明用户可见行为变化
- CMake 构建配置：`CMakeLists.txt` 变更需先确认 Archicad 29 限制与 DevKit 路径
- 若新增/改动脚本：必须同步更新 `deploy.sh` 的拷贝或执行步骤

## ⚠️ vibe coding 行为约束

> 本项目由非程序员主导，Codex是执行者。以下规则防止屎山代码和技术债务累积。

### 接到任务前必须做的事

1. 收到模糊需求先问清楚：用户是谁？成功标准是什么？有没有现有代码可以复用？
2. 涉及超过50行代码或多个文件时，必须先输出计划等确认：目标/影响文件/步骤/不做的事/验证方法
3. 修改前必须先读相关文件，不能凭假设修改

### 写代码时的硬性规则

- 每个组件/模块只做一件事，不要把所有逻辑堆在一个文件里
- 公共逻辑提取到utils/或hooks/，不要复制粘贴
- 每个函数只做一件事，超过30行考虑拆分
- 关键逻辑必须加注释
- 错误必须显示给用户，禁止console.log了事或静默失败
- 禁止硬编码URL、端口、密钥——用环境变量或常量文件
- 禁止一次性修改超过3个无关文件

### 完成任务后必须做的事

1. 给出验证步骤（打开哪个页面，做什么操作，预期结果是什么）
2. 提示commit：`git add . && git commit -m "功能描述" && git push origin main`
3. 涉及新踩坑、架构变化、新依赖时，提示更新AGENTS.md

### 遇到问题时的原则

- 先读错误信息定位原因，不要盲目试错
- 修了一个bug引入另一个bug，立刻告知，不要继续叠加修复
- 对技术方案不确定时，给两个选项让用户决策
- 发现现有代码潜在问题，即使不影响当前任务也要主动指出
