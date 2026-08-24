# FlashSafe Pro 原生上位机设计

## 目标

用 Windows 原生桌面程序替换浏览器工作台，并将项目中可由 PC 执行的操作集中到一个窗口：串口 CLI、OTA 命令、OTA 固件服务器、A/B 发布、密钥轮换与 ROM 串口烧录。

## 方案

使用 Python Tkinter，因其随当前 Python 提供、不需要浏览器或额外 GUI 运行时。`tools/desktop_app.py` 在启动时定位含 `platformio.ini` 的项目根目录，设置 `FLASHSAFE_PROJECT_ROOT` 后复用 `tools/webui.py` 的串口和受限子进程实现；不会运行 HTTP Web UI。PyInstaller 生成单文件 `dist/FlashSafeProHost.exe`，桌面快捷方式的“起始位置”设为项目根目录，确保 EXE 可调用本项目的 PlatformIO、固件与密钥。

## 界面与错误处理

窗口包含设备串口日志/命令、OTA URL、发布和密钥轮换、固件服务器、分区烧录以及本机任务日志。耗时构建与烧录由已有后台进程执行，Tkinter 定时刷新状态。COM 口独占、端口/版本/目标校验与 BOOT0 人工提示沿用原有安全边界；窗口关闭时关闭本程序拥有的串口及固件服务器。
