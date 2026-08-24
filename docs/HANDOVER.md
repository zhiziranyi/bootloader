# FlashSafe Pro 项目交接文档

更新时间：2026-08-24
目标板：STM32F407ZGT6 + LAN8720 + W25Q64 + SSD1306 OLED

## 1. 项目状态

项目实现安全 A/B OTA Bootloader：LAN8720 通过 DHCP/HTTP 获取固件，先写入 W25Q64 下载区，再执行 CRC32、包头公钥哈希和 ECDSA P-256 签名校验。验证通过后，Bootloader 仅擦写非活动槽；新镜像以 Trial 状态启动，APP 自动确认后才切换 ACTIVE 槽。

分区如下：

| 区域 | 地址 | 大小 |
|---|---:|---:|
| Bootloader | `0x08000000` | 128 KB |
| Slot A | `0x08020000` | 384 KB |
| Slot B | `0x08080000` | 384 KB |
| Config journal | `0x080E0000` | 128 KB |
| W25Q64 Download | `0x000000` | 2 MB |
| W25Q64 Factory | `0x200000` | 2 MB |

## 2. 已完成的实板验证

| 能力 | 结果与证据 |
|---|---|
| 串口 ROM 初始烧录 | 已通过 COM3 / STM32 ROM 下载模式烧录 Bootloader 和分槽 APP。 |
| OLED 与 W25Q64 | OLED 检出 `0x3C`，W25Q64 ID 为 `0xEF4017`。 |
| LAN8720 网络 | Windows ICS 下设备可 DHCP 获得 `192.168.137.x` 地址，并从 `192.168.137.1:8000` 下载。 |
| 普通 OTA | A/B 双方向安装、HTTP Range 下载、CRC 与 ECDSA 验签均已通过。 |
| Trial 确认 | 新 APP 自动打印 `Trial firmware confirmed`，ACTIVE 槽持久化切换成功。 |
| 回滚 | 从 Slot B 回滚到 Slot A 成功，`Rollbacks: 1`。 |
| 密钥轮换 | 新 Bootloader 公钥 SHA-256 已变更为 `02207B7869DD7F3CCE7F5224036F4BD3EC22C178825D7149C90C71DF57E3B9C5`。 |
| 轮换后 OTA | 新密钥签名的 v2.0.0 成功安装到 Slot B、Trial 启动并确认。 |
| 上位机 | 原生 Tkinter 上位机支持串口 CLI、发布、HTTP 服务、串口烧录、密钥轮换、自动 OTA URL 和操作记录。 |

最终已确认状态：`ACTIVE=B`、`FW=2.0.0`、`Upgrade State=8 (DONE)`、`Rollbacks=1`。

## 3. 日常操作

1. 用桌面快捷方式启动 `FlashSafe Pro 上位机`，关闭其他串口软件并连接 `COM3 / 115200`。
2. 手动复位，在 3 秒窗口点击“复位后进入 CLI”，用 `info` 查看 `ACTIVE=A/B`。
3. 在“发布、服务器与烧录”中填写递增版本号，点击“构建 A/B 并发布”。
4. 启动端口 8000 的固件服务器。
5. OTA 页会根据 `ACTIVE` 自动选择相反槽位的最新包并生成 URL；发送 `upgrade net`。
6. 看到 `Firmware signature VALID` 后执行 `reboot`；等待 Trial APP 自动确认。
7. 再次进入 CLI，使用 `info` 确认活动槽和版本。

日常升级不要直接烧录 Bootloader。只有首次部署、救援或密钥轮换后才进入 STM32 ROM 串口下载模式：`BOOT0=3.3V`、复位、烧录；完成后将 `BOOT0=GND` 并复位。

## 4. 密钥管理

- 私钥：`tools/keys/private_key.pem`，已被 `.gitignore` 排除，严禁提交或外发。
- Bootloader 信任根：`include/public_key.h`，包含可公开的验证公钥。
- “轮换密钥并重建”会自动备份旧私钥和旧公钥至 `tools/keys/backups/`，生成新密钥、重建 Bootloader，并生成新的 A/B 签名包。
- 轮换后必须用串口重新烧录 Bootloader；旧 Bootloader 不信任新私钥签出的包，新 Bootloader 也会拒绝旧私钥签出的包。

## 5. 未完成的实板测试

1. **断电注入测试**：分别在下载 33%/67%、内部 Flash 安装和 Trial 未确认阶段断电，检查重启后是否正确恢复、回滚或拒绝不完整包。
2. **负向安全测试**：故意篡改 `.pkg`、使用旧密钥签名、发送低版本包，验证 CRC、签名和 anti-rollback 拒绝路径。
3. **Factory restore**：代码支持从 W25Q64 Factory 区恢复 Slot A，但当前没有“工厂镜像制备/写入”工具；Factory 区未预置有效签名镜像时不要执行 `factory`。

## 6. 推荐后续优化（按含金量排序）

1. **Factory image provisioning 工具**：实现签名 Factory 包写入 W25Q64 `0x200000`、读回校验和一键恢复测试。补齐量产恢复能力。
2. **可重复的断电故障注入**：用继电器或可控电源，在下载、安装、Trial 的状态机节点自动断电，输出恢复测试报告。这能直接体现嵌入式可靠性工程能力。
3. **每槽版本元数据**：当前 `info`/OLED 的 `FW` 是最近已验证版本；手动回滚后可能仍显示较新的版本。记录 `slot_a_version` / `slot_b_version` 后可准确展示每个槽实际镜像版本。
4. **CI 与发布治理**：GitHub Actions 运行 Host 单元测试、构建固件、产生 SHA-256 清单；私钥只通过 GitHub Secret 注入签名任务，绝不进入仓库。
5. **安全升级策略强化**：增加签名的单调构建号、密钥版本号和撤销策略，抵御合法旧包重放；HTTP 已由 ECDSA 保证真实性，但 TLS 可提升传输保密性与抗流量分析能力。

## 7. 仓库卫生与构建

提交源码、文档、公开公钥和测试；不提交私钥、`.pkg/.bin`、PlatformIO/pyinstaller 构建目录或桌面 EXE。发布包可通过 GitHub Releases 或受控固件服务器分发。

常用验证命令：

```powershell
python -m unittest discover -s .\tools\tests -p "test_*.py"
pio run -e black_f407zg
pio run -d app -e app_a
pio run -d app -e app_b
```
