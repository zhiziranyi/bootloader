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
| Trial 未确认掉电回退 | v2.0.12 在延迟确认窗口断电，下一次启动显示 `Trial was not confirmed; reverting to slot A`。 |
| Factory 制备/恢复 | 已验签的 v2.0.15 写入 W25Q64 Factory 区；`factory` 已验签并恢复至 Slot A。 |
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

## 5. 硬件在环证据报告

上位机“硬件在环报告”页读取真实 MCU UART 输出，并可导出 `reports/hil-report-*.md`。每个完整通过项会自动保存到 `reports/hil-evidence.json`；下次启动会显示“历史保存证据”，因此可从仍为“待验证”的项目继续。首次启动会迁移已有 Markdown 报告中包含完整 UART 证据的通过项。可在上位机中清除该台账后重新开始。它把以下场景明确分为“通过”或“待验证”：安全 OTA 验签、Trial 确认、下载阶段恢复、安装阶段恢复、Trial 未确认回退、Factory 制备和 Factory 恢复。

下载、安装和 Trial 三类掉电均由测试人员实际操作；未出现恢复日志时报告不会声称通过。这份 Markdown 可作为答辩、简历项目或后续回归测试的硬件证据附件。

## 6. 待执行的实板测试

1. **断电注入测试**：已新增下载续传、安装保持点和 Trial 延迟确认的完整操作支持；按 [POWER_FAIL_AND_FACTORY_VALIDATION.md](POWER_FAIL_AND_FACTORY_VALIDATION.md) 分别在下载 33%/67%、内部 Flash 安装和 Trial 未确认阶段执行真实断电，并保存恢复日志。
2. **负向安全测试**：故意篡改 `.pkg`、使用旧密钥签名、发送低版本包，验证 CRC、签名和 anti-rollback 拒绝路径。
3. **完整断电覆盖**：Trial 未确认回退已通过；下载阶段与内部 Flash 安装阶段仍需要在实际保持点执行断电并保存恢复日志。

## 7. 推荐后续优化（按含金量排序）

1. **可重复的断电故障注入**：用继电器或可控电源，在下载、安装、Trial 的状态机节点自动断电，输出恢复测试报告。这能直接体现嵌入式可靠性工程能力。
2. **每槽版本元数据**：当前 `info`/OLED 的 `FW` 是最近已验证版本；手动回滚后可能仍显示较新的版本。记录 `slot_a_version` / `slot_b_version` 后可准确展示每个槽实际镜像版本。
3. **CI 与发布治理**：GitHub Actions 运行 Host 单元测试、构建固件、产生 SHA-256 清单；私钥只通过 GitHub Secret 注入签名任务，绝不进入仓库。
4. **安全升级策略强化**：增加签名的单调构建号、密钥版本号和撤销策略，抵御合法旧包重放；HTTP 已由 ECDSA 保证真实性，但 TLS 可提升传输保密性与抗流量分析能力。

## 8. 仓库卫生与构建

提交源码、文档、公开公钥和测试；不提交私钥、`.pkg/.bin`、PlatformIO/pyinstaller 构建目录或桌面 EXE。发布包可通过 GitHub Releases 或受控固件服务器分发。

常用验证命令：

```powershell
python -m unittest discover -s .\tools\tests -p "test_*.py"
pio run -e black_f407zg
pio run -d app -e app_a
pio run -d app -e app_b
```
