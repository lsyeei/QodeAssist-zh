# QodeAssist 中文优化版

简体中文 | [English](README_EN.md)

基于 [QodeAssist](https://github.com/Palmirka/QodeAssist) 开源项目的定制化修改版本。

## 项目简介

QodeAssist 是一个 Qt Creator 插件，提供 AI 驱动的代码补全和聊天助手功能。本项目在其基础上进行了针对中文用户的优化和改进。

## 主要修改内容

### 1. 界面布局优化
- **ChatItem.qml 边距修复**：修复用户消息控件的边距问题，优化聊天界面显示
- **自适应宽度调整**：文本较短时自动调整宽度，提升视觉体验
- **新增图标资源**：添加编辑、折叠、滚动到底部等 SVG 图标

### 2. 国际化支持增强
- **翻译修复**：修复多处 `TrConstants::` 未正确包裹 `Tr::tr()` 的问题
  - `GeneralSettings.cpp`
  - `ChatAssistantSettings.cpp`
  - `QuickRefactorSettings.cpp`
- **语言切换功能**：添加界面语言切换支持（设置页面中可选）

### 3. 功能改进
- **ChatSerializer**：优化聊天记录序列化
- **ChatUtils**：聊天工具函数增强
- **CodeBlock**：代码块显示优化

## 安装说明

### 编译要求
- Qt 6.8.1 或更高版本
- Qt Creator 17+ 或 19+
- CMake 3.16+

### 编译步骤

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### 安装插件

将编译生成的插件文件复制到 Qt Creator 插件目录：
- **Windows**: `%LOCALAPPDATA%/QtProject/qtcreator/plugins/<版本>/`
- **Linux**: `~/.local/share/data/QtProject/qtcreator/plugins/<版本>/`
- **macOS**: `~/Library/Application Support/QtProject/Qt Creator/plugins/<版本>/`

## 使用说明

### 启用插件
1. 打开 Qt Creator
2. 进入 `帮助` → `关于插件`
3. 启用 QodeAssist 插件
4. 重启 Qt Creator

### 配置 AI 服务
1. 打开 `工具` → `选项` → `QodeAssist` → `General`
2. 配置代码补全和聊天助手的 AI 服务提供商
3. 支持 Ollama、OpenAI、Claude 等多种后端

### 语言切换
1. 打开 QodeAssist 设置页面
2. 在 Language 下拉框中选择界面语言
3. 重启 Qt Creator 生效

## 项目结构

```
QodeAssist-zh/
├── ChatView/          # 聊天视图 QML 组件
├── settings/          # 设置页面
│   ├── GeneralSettings.cpp      # 通用设置
│   ├── CodeCompletionSettings.cpp  # 代码补全设置
│   ├── ChatAssistantSettings.cpp   # 聊天助手设置
│   └── ...
├── providers/         # AI 服务提供商
├── templates/         # 提示词模板
├── context/           # 上下文管理
├── chat/              # 聊天功能
└── resources/         # 资源文件
    └── translations/  # 翻译文件
```

## 与原版的区别

| 功能 | 原版 QodeAssist | 本修改版 |
|------|-----------------|----------|
| 界面语言 | 英文 | 中文/英文可切换 |
| 界面布局 | 默认 | 优化边距和自适应宽度 |
| 翻译完整性 | 部分 | 完整翻译修复 |

## 开源协议

本项目基于 [GPL-3.0](LICENSE) 协议开源。

原项目 QodeAssist 版权归属：[Petr Mironychev](https://github.com/Palmirka)

## 贡献与反馈

欢迎提交 Issue 和 Pull Request！

## 致谢

感谢 QodeAssist 原作者的开源贡献，为本项目提供了坚实的基础。
