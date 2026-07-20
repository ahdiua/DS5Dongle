# Pico2W DualSense 5 Bridge
[English](./README.md)
> 将 Pico2W 变成 DS5 手柄的无线适配器

# 功能特点
 - 支持HD震动
 - 支持在运行时切换为 Windows 10/11 可识别的 Xbox 360 兼容 XInput 手柄

# 使用方法
1. 按住 Pico 上的BOOTSEL进入刷机
2. 将 .uf2 文件拖入进去
3. 将 DS5 手柄进入蓝牙配对模式
4. Enjoy it

***你可能需要在控制器处于匹配模式时重新插拔 pico***

- 手柄连接到pico以后，系统才会显示设备

# BOOTSEL 按键功能

固件运行时，Pico2W 板载的 BOOTSEL 按键支持以下操作：

- 单击：扫描/切换已配对的手柄
- 双击：正常重启 Pico
- 三击：重启并进入 BOOTSEL 刷机模式
- 四击：在原生 DualSense 模式和 Xbox 360 兼容 XInput 模式之间切换
- 长按约 1.5 秒：清除所有已保存的手柄配对

四击切换后，Pico 会断开并重新枚举 USB 设备，但不会断开当前蓝牙手柄。
板载 LED 闪烁一次表示已切换到原生 DualSense 模式，闪烁两次表示已切换到
XInput 模式。

XInput 模式适用于需要 Xbox 手柄的 Windows 10/11 游戏，支持常规按键、方向键、
摇杆、模拟扳机、Guide 键和双马达震动。触摸板无论按在左侧还是右侧，点击都会
映射为 Xbox Back/View 键；该模式不提供触摸位置数据、陀螺仪、手柄音频、
自适应扳机和网页配置。此切换不会写入闪存，断电或重启后会自动恢复原生
DualSense 模式；再次四击即可立即切回。XInput 模式仅面向 Windows PC，不能让
适配器连接 Xbox 主机。

# Pico 配置调整
你可以通过网页调整Pico的内部设置

- 用于正式固件: https://ds5.awalol.eu.org
- 用于测试固件: https://ds5-dev.awalol.eu.org

### Pico W 版本

Pico W 由于性能问题，只能支持震动，不支持扬声器。
你可以通过开启 `-DPICO_W_BUILD=ON` 编译项去开启 Pico W 固件编译，或者在 Github Action 下载预编译的固件

### USB 唤醒支持

标准固件已经内置 PS 键唤醒功能，不再需要单独的 `feat/usb-wake` 分支或
`ds5-bridge-wake.uf2`。该功能默认关闭，可在网页配置中打开“按 PS 键从睡眠中唤醒
PC”。启用后，适配器会增加 HID 键盘接口并声明 USB 远程唤醒能力；关闭时不会枚举
该接口。

极为建议在使用该功能前阅读  #60 和 #61

### 社区分支
https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition
https://github.com/zurce/DS5Dongle-OLED

# 当前问题:
- 声音可能有点小卡顿

# 性能

音频编解码、重采样以及蓝牙/USB 热路径现在从 RAM 执行。完整音频路径可在默认的
150 MHz 时钟下运行，不再需要旧版本使用的 320 MHz、1.2 V 超频设置。

# 未来计划
请查看[DS5Dongle plan](https://github.com/users/awalol/projects/5)

# 编译

### Windows 11（PowerShell 7，无需 WSL）

如果已经克隆仓库，请在仓库根目录运行：

```powershell
pwsh .\tools\build-windows.ps1
```

也可以只下载 [`tools/build-windows.ps1`](tools/build-windows.ps1)，然后在其所在目录运行：

```powershell
pwsh .\build-windows.ps1
```

脚本会准备 CMake、Ninja、Python、Git、ARM GNU 工具链、Pico SDK 和 TinyUSB，
并将依赖环境放在 `%USERPROFILE%\.ds5-build`。生成的 `ds5-bridge.uf2` 会复制到
脚本目录和桌面。重复运行时会复用已经安装的依赖。

### Windows 构建环境清理

默认清理当前仓库的 `build` 目录，以及清理脚本旁生成的 UF2：

```powershell
pwsh .\tools\clean-windows.ps1
```

预览完整清理（包括 `%USERPROFILE%\.ds5-build` 依赖环境和桌面固件副本）：

```powershell
pwsh .\tools\clean-windows.ps1 -All -WhatIf
```

确认预览无误后去掉 `-WhatIf` 即可执行。可用 `-Desktop` 仅增加桌面 UF2 清理，
或用 `-Dependencies` 增加 `.ds5-build` 清理；`-All` 同时启用两者。如果托管的
DS5Dongle 克隆中有未提交文件，脚本会拒绝删除，只有确认要丢弃这些文件时才使用
`-Force`。通过 `winget` 安装的系统级工具不会被卸载。

### 其他平台

手动构建需要 Pico SDK 2.3.0，并将 SDK 内的 TinyUSB 子模块切换到 0.21.0。然后运行：

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH=<sdk路径>
cmake --build build --target ds5-bridge
```

# 致谢
 - [rafaelvaloto/Pico_W-Dualsense](https://github.com/rafaelvaloto/Pico_W-Dualsense) - 灵感来源
 - [egormanga/SAxense](https://github.com/egormanga/SAxense) - 震动报文
 - [https://controllers.fandom.com/wiki/Sony_DualSense](https://controllers.fandom.com/wiki/Sony_DualSense) - 数据报文结构
 - [Paliverse/DualSenseX](https://github.com/Paliverse/DualSenseX) - 扬声器数据包报文
