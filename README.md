# GDL Copilot

Archicad 内嵌 AI GDL 修复助手（OpenBrep 的 Archicad 插件版本）。

## 功能特性

- 在 Archicad 内直接打开 Copilot 面板，无需离开建模环境。
- 粘贴 GDL 报错或代码片段后，返回可直接使用的修复建议与代码块。
- 支持对话式迭代修复，逐步定位问题并优化脚本。
- 提供代码块一键复制，快速回填到 GDL 编辑器。

## 前置要求

- 已安装 OpenBrep Add-On（Archicad 插件）。
- Archicad 29（AC29）。
- Python 3 环境（建议 3.10+），并可运行 `uvicorn`。

## 安装方法

1. 编译生成 `OpenBrep.bundle`（APX 打包产物）。
2. 将 bundle 放入 Archicad Add-Ons 目录：
   - `/Applications/GRAPHISOFT/Archicad 29/Add-Ons/`
3. 在 Archicad 中加载并启用该 Add-On。

## 启动方式

在项目根目录启动 Copilot 服务：

```bash
cd ~/MAC工作/工作/code/开源项目/openbrep-addon
python -m uvicorn copilot.server:app --port 8502
```

## 使用方式

1. 在 Archicad 菜单中打开 `OpenBrep -> GDL Copilot`。
2. 粘贴编译报错或问题代码。
3. 获取 AI 修复结果并复制回 GDL 编辑器验证。

## 与 OpenBrep 的关系

- GDL Copilot 是 OpenBrep 在 Archicad 侧的插件化能力扩展。
- 面板、菜单与生命周期由 OpenBrep Add-On 承载。
- AI 推理与配置依赖 OpenBrep / gdl-agent 相关运行环境。

## 注意事项

- 当前仅支持 Archicad 29（AC29）。
- Copilot 服务依赖本地 `localhost:8502`。
- 面板版交互与布局仍在持续优化中。
