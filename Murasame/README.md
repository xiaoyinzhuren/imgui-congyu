# Murasame

基于 ImGui + DirectX 11 的桌面工具应用，支持中英双语切换，带透明登录卡片。

## 功能

- **数据面板**：计数器、滑块、开关和文本输入控件演示
- **列表管理**：添加、删除、管理列表项目
- **监控面板**：实时帧率、帧耗时、系统信息、进度条
- **设置**：语言切换（中文 / English）、主题配色自定义、圆角调节

## 构建

### 环境要求

- Visual Studio 2022+ (v145 工具集)
- Windows 10 SDK
- DirectX 11

### 步骤

1. 打开 `Project2.sln`
2. 选择 Release | x64
3. 生成 → 生成解决方案

### 字体

项目使用微软雅黑 (`fonts/msyh.ttf`)。如果字体加载失败，ImGui 将回退到内置默认字体。

### 角色图片

将角色 PNG 图片放置到 `assets/character.png`，程序启动时自动加载。

## 默认登录密钥

`admin123`

## 文件结构

```
Murasame/
├── Project2.sln                  # VS 解决方案
├── Project2/                     # 主项目源码
│   ├── Project2.vcxproj
│   ├── Project2.vcxproj.filters
│   └── main.cpp                  # 全部源码
├── imgui/                        # ImGui 库
├── fonts/                        # 字体文件
│   └── msyh.ttf
├── assets/                       # 资源文件
│   └── character.png             # 角色图片（需自行添加）
├── .gitignore
└── README.md
```

## 许可

MIT License
