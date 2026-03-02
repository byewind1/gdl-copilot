# openbrep-addon / CLAUDE.md

## 项目定位

- C++ 项目：Archicad 29 插件壳（OpenBrep Add-On）。
- 负责在 Archicad 内承载 GDL Copilot 的菜单、面板与生命周期。
- 依赖 `gdl-agent` 在 `localhost:8502` 提供 AI 服务。

## 架构与模块职责

- 插件入口与生命周期
  - `Sources/AddOnMain.cpp`：`CheckEnvironment` / `RegisterInterface` / `Initialize` / `FreeData`。
- 菜单与命令
  - `Sources/OpenBrepCommands.hpp/.cpp`：菜单命令分发与 Copilot 启动逻辑。
  - `RFIX/OpenBrepFix.grc`：菜单资源与 MDID。
- Copilot 面板
  - `Sources/CopilotPalette.hpp/.cpp`：`DG::Palette + DG::Browser` 面板实现与尺寸管理。
  - 资源 ID：`CopilotPaletteResId = 32510`。
- 资源与打包
  - `RINT/OpenBrep.grc` / `RFIX/OpenBrepFix.grc`：本地化与非本地化资源。
  - `CMakeLists.txt`：AC29 构建与 bundle 打包。

## 与 gdl-agent 的接口关系

- Add-On 内嵌 Browser 默认加载：`http://localhost:8502`
- `copilot/server.py` 作为桥接服务，读取 `gdl-agent/config.toml` 并调用 `openbrep` 能力。
- 若端口、请求格式、返回结构变更，需同步更新 `copilot/server.py` 与前端 `copilot/index.html`。

## 构建与部署

- 产物：`build/OpenBrep.bundle`
- 目标目录：`/Applications/GRAPHISOFT/Archicad 29/Add-Ons/`
- 标准部署步骤（必须）：
  1. `rm -rf "/Applications/GRAPHISOFT/Archicad 29/Add-Ons/OpenBrep.bundle"`
  2. `cp -r build/OpenBrep.bundle "/Applications/GRAPHISOFT/Archicad 29/Add-Ons/"`

## 开发注意事项

- 本项目仅支持 Archicad 29（AC29）。
- AC API 相关实现优先参考 Tapir 工程实践，不做拍脑袋式 API 推断。
- 面板布局问题优先检查：`CopilotPalette` 客户区尺寸同步 + `copilot/index.html` 在 DG::Browser 内的适配。
- 改 `copilot/` 下 Python/HTML 不需要重编 C++；改 `Sources/` 或 `.grc` 需要重新 `make` 并部署 bundle。

## 会话启动期望

- Claude Code session 启动后应先读取本文件，按上述模块边界与部署规范执行。
