# FlashSafe Pro

面向 **STM32F407ZGT6** 的安全网络 OTA Bootloader：LAN8720 以太网下载、W25Q64 外部 Flash 暂存、ECDSA P-256 签名验证、A/B 双槽试运行确认与自动回滚。

实板已完成完整闭环：

```text
Slot A v1.2.0 -> Slot B v1.2.1 -> Slot A v1.2.2
-> Slot B v1.2.6 -> rollback Slot A -> Slot B v2.0.0 (new signing key)
```

已实测 HTTP Range 下载、外部 Flash 读回、CRC、ECDSA 签名校验、内部 Flash 安装、试运行确认、持久化 A/B 切换、手动回滚，以及密钥轮换后的新信任链 OTA。完整交接和验证记录见 [docs/HANDOVER.md](docs/HANDOVER.md)。

## 功能亮点

- A/B 双槽：App-A `0x08020000`、App-B `0x08080000`，新镜像写入非活动槽。
- 断电安全：下载、验签、安装与试运行状态持久化；未确认试运行会回到旧活动槽。
- 安全校验：SHA-256 + ECDSA secp256r1/P-256，校验包头、公钥哈希、镜像 CRC 与签名。
- 网络 OTA：LAN8720 RMII、LwIP、DHCP、HTTP Range 分块下载。
- W25Q64 暂存：下载区与 Factory 区，安装关键阶段包含读回检查。
- 可观测性：USART1 CLI、SSD1306 OLED、下载进度、密钥哈希和错误日志。
- 可发布性：一条命令构建 A/B、签名、验签并生成 SHA-256 发布清单。

## 分区

| 区域 | 起始地址 | 大小 | 用途 |
|---|---:|---:|---|
| Bootloader | `0x08000000` | 128 KB | 启动、OTA、验签、回滚 |
| Slot A | `0x08020000` | 384 KB | App-A |
| Slot B | `0x08080000` | 384 KB | App-B |
| Config journal | `0x080E0000` | 128 KB | 升级状态、版本、确认记录 |
| W25Q64 Download | `0x000000` | 2 MB | OTA 缓存 |
| W25Q64 Factory | `0x200000` | 2 MB | 出厂镜像 |

包格式：`[60B Header] [Image] [72B ECDSA Signature Block]`。

## 接线

所有模块使用 **3.3 V 逻辑电平** 且必须共地。

### USB-TTL / CLI

| USB-TTL | STM32F407 | 用途 |
|---|---|---|
| TXD | PA10 | USART1 RX |
| RXD | PA9 | USART1 TX |
| GND | GND | 共地 |

串口为 115200、8N1、无流控。STM32 ROM 下载时将 `BOOT0` 拉高、复位、烧录；下载完成后拉低 `BOOT0` 并复位。

### SSD1306 I2C OLED

| OLED | STM32F407 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SCL | PB6 |
| SDA | PB7 |

驱动探测 `0x3C/0x3D`；不要让 I2C 上拉到 5 V。

### W25Q64

| W25Q64 | STM32F407 |
|---|---|
| VCC / GND | 3.3 V / GND |
| CLK | PC10 |
| DO / MISO | PC11 |
| DI / MOSI | PC12 |
| CS | PD2 |

### LAN8720 RMII

| LAN8720 | STM32F407 |
|---|---|
| VCC / GND | 3.3 V / GND |
| REF_CLK | PA1 |
| MDIO / MDC | PA2 / PC1 |
| CRS_DV | PA7 |
| RXD0 / RXD1 | PC4 / PC5 |
| TX_EN | PB11 |
| TXD0 / TXD1 | PB12 / PB13 |
| nRST | PD3 |

直连笔记本网口时，可在 Windows WLAN 的“共享”中共享给以太网接口。

## 首次烧录

上传脚本限制了起始地址和擦写长度，所以烧录任一环境只改自己的分区。

```powershell
# Bootloader -> 0x08000000
pio run -e black_f407zg -t upload --upload-port COM3

# 仅用于直接放置测试 App
pio run -d app -e app_a -t upload --upload-port COM3
pio run -d app -e app_b -t upload --upload-port COM3
```

日常 OTA 不需要重刷 Bootloader 或直接烧录 App。

## 一键发布

```powershell
python .\tools\release_firmware.py --version 1.2.3
```

该命令会写入 App 版本、构建 App-A/App-B、分别打包、验签，并输出：

```text
firmware_v1.2.3_slotA.pkg
firmware_v1.2.3_slotB.pkg
release-v1.2.3.json
```

清单包含每个包的目标槽、长度和 SHA-256。脚本不连接硬件、不上传固件、不轮换密钥。

## OTA 操作

启动本地服务器：

```powershell
python .\tools\fw_server.py --dir .\firmware --port 8000
```

板子上电 3 秒内发送任意字符进入 CLI。根据当前活动槽选择 **非活动槽** 的包：

| 当前活动槽 | 应升级到 | 包 |
|---|---|---|
| A | B | `firmware_vX.Y.Z_slotB.pkg` |
| B | A | `firmware_vX.Y.Z_slotA.pkg` |

例如当前 A 槽升级到 B：

```text
upgrade net 192.168.137.1:8000/firmware_v1.2.3_slotB.pkg
```

出现 `Firmware verified. Type 'reboot' to install.` 后输入 `reboot`。新 App 首次应显示：

```text
[APP] Trial firmware confirmed
FlashSafe Pro App v1.2.3
```

再复位一次；显示 `ACTIVE <目标槽>` 与 `STATE DONE` 即表示升级持久化。若 App 未确认，下一次复位会自动回退。

## CLI

```text
info
status
upgrade net <host:port/path.pkg>
rollback
factory
key show
reboot
help
```

## Windows 原生上位机

桌面快捷方式：`C:\Users\13957\Desktop\FlashSafe Pro 上位机.lnk`。

它指向项目内的 `dist\FlashSafeProHost.exe`，不使用浏览器、不启动网页服务。EXE 会以项目根目录作为工作目录，因此能找到本项目的 PlatformIO、密钥和 `firmware\`。

原生窗口可在一个界面完成：

- 枚举、连接/断开 COM 口，查看设备实时日志，发送 `help`、`status`、`reboot` 或任意 CLI 命令；
- 启动/停止 `firmware/` 的 HTTP Range 固件服务器（默认端口 `8000`）；
- 通过 URL 发送 `upgrade net <host:port/path.pkg>` OTA 命令；
- 一键构建、签名、验签并发布 A/B 两个 `.pkg`；
- 选择 Bootloader、APP-A 或 APP-B，调用 PlatformIO 的 STM32 ROM 串口烧录；
- 打开本地固件/密钥目录，并保留量产密钥轮换。

原生上位机本身不监听网络端口。由上位机启动的固件服务器为便于 LAN8720 访问，会监听本机网卡；使用 Windows 网络共享时继续让板端访问 `192.168.137.1:8000`。

串口一次只能由一个程序打开：使用上位机前必须关闭友善串口助手。上位机不能替代 BOOT0 接线或物理复位：进入 Bootloader CLI 时先手动复位，再在 3 秒内点击“发送任意字符进入 CLI”；ROM 串口烧录时按页面提示设置 BOOT0/复位。

## 验证

```powershell
python -m unittest discover -s tools\tests -p "test_*.py"
python .\tools\verify_pkg.py --pkg .\firmware\firmware_v1.2.2_slotA.pkg --pubkey .\include\public_key.h --expected-slot A
```

当前自动测试覆盖分区、串口分区擦写、包结构、签名/CRC、下载状态、W25Q64、App 跳转、试运行确认和发布清单。

## 安全边界

- 开发密钥仅用于联调；量产前必须生成新 secp256r1 密钥并重刷 Bootloader。
- ECDSA 保证来源和完整性；HTTP 不提供传输保密性或抗拒绝服务能力。
- `tools/keys/private_key.pem` 不得发送、上传或提交公开仓库。
- OTA 必须使用目标为**非活动槽**的包，才能保留已确认镜像用于回退。

## 简历项目描述

**FlashSafe Pro — STM32F407 安全 A/B 网络 OTA Bootloader**

基于 STM32F407ZGT6 实现 LAN8720 + LwIP HTTP OTA Bootloader，使用 W25Q64 进行分块下载暂存，并通过 SHA-256/ECDSA P-256 验签保护固件来源。设计 A/B 双槽试运行确认、掉电恢复与自动回滚机制；实现受限分区串口下载、OLED/CLI 状态可观测性和自动化 A/B 构建签名发布流程，并已在实板完成 `A -> B -> A` 双向 OTA 验证。
