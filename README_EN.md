# QodeAssist Chinese Optimized Version

[简体中文](README.md) | English

A customized and enhanced version based on the [QodeAssist](https://github.com/Palmirka/QodeAssist) open-source project.

## Project Introduction

QodeAssist is a Qt Creator plugin that provides AI-powered code completion and chat assistant functionality. This project builds upon the original with optimizations and improvements tailored for Chinese users.

## Key Modifications

### 1. UI Layout Optimizations
- **ChatItem.qml Margin Fix**: Fixed margin issues for user message controls, optimizing chat interface display
- **Adaptive Width Adjustment**: Automatically adjusts width for short text, enhancing visual experience
- **New Icon Resources**: Added SVG icons for edit, collapse, scroll-to-bottom, and more

### 2. Enhanced Internationalization Support
- **Translation Fixes**: Fixed multiple instances where `TrConstants::` was not properly wrapped with `Tr::tr()`
  - `GeneralSettings.cpp`
  - `ChatAssistantSettings.cpp`
  - `QuickRefactorSettings.cpp`
- **Language Switching**: Added interface language switching support (selectable in settings page)

### 3. Functional Improvements
- **ChatSerializer**: Optimized chat history serialization
- **ChatUtils**: Enhanced chat utility functions
- **CodeBlock**: Improved code block display

## Installation Instructions

### Build Requirements
- Qt 6.8.1 or higher
- Qt Creator 17+ or 19+
- CMake 3.16+

### Build Steps

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Install Plugin

Copy the compiled plugin files to the Qt Creator plugins directory:
- **Windows**: `%LOCALAPPDATA%/QtProject/qtcreator/plugins/<version>/`
- **Linux**: `~/.local/share/data/QtProject/qtcreator/plugins/<version>/`
- **macOS**: `~/Library/Application Support/QtProject/Qt Creator/plugins/<version>/`

## Usage Guide

### Enable Plugin
1. Open Qt Creator
2. Go to `Help` → `About Plugins`
3. Enable the QodeAssist plugin
4. Restart Qt Creator

### Configure AI Service
1. Open `Tools` → `Options` → `QodeAssist` → `General`
2. Configure AI service providers for code completion and chat assistant
3. Supports multiple backends: Ollama, OpenAI, Claude, etc.

### Language Switching
1. Open the QodeAssist settings page
2. Select interface language from the Language dropdown
3. Restart Qt Creator to apply changes

## Project Structure

```
QodeAssist-zh/
├── ChatView/          # Chat view QML components
├── settings/          # Settings pages
│   ├── GeneralSettings.cpp      # General settings
│   ├── CodeCompletionSettings.cpp  # Code completion settings
│   ├── ChatAssistantSettings.cpp   # Chat assistant settings
│   └── ...
├── providers/         # AI service providers
├── templates/         # Prompt templates
├── context/           # Context management
├── chat/              # Chat functionality
└── resources/         # Resource files
    └── translations/  # Translation files
```

## Differences from Original

| Feature | Original QodeAssist | This Modified Version |
|---------|---------------------|----------------------|
| Interface Language | English | Chinese/English Switchable |
| UI Layout | Default | Optimized margins and adaptive width |
| Translation Completeness | Partial | Complete translation fixes |

## Open Source License

This project is open-sourced under the [GPL-3.0](LICENSE) license.

Original QodeAssist copyright: [Petr Mironychev](https://github.com/Palmirka)

## Contributing and Feedback

Issues and Pull Requests are welcome!

## Acknowledgments

Thanks to the original QodeAssist author for the open-source contribution, which provided a solid foundation for this project.
