# 掉电恢复与 Factory 恢复验证

本文用于在 STM32F407ZGT6 + W25Q64 + LAN8720 实物上完成可复现的可靠性验证。
执行时保持电脑固件服务器运行，并使用上位机独占 COM3。

## 前置条件

- Bootloader 已烧录为包含 `factory net` 和 `test power install` 的版本。
- 上位机已连接设备、能够进入 CLI，且固件服务器正在运行。
- 当前 A、B 两槽至少各有一个可启动应用；用 `info` 记录当前 `Active Slot`。
- 每一项开始前，用 `info` 和 `status` 截取基线日志。

## 1. 下载阶段断电与续传

1. 发布比当前版本高的普通 A/B 包，并选择与 ACTIVE 相反的包。
2. 发送 `upgrade net <URL>`。
3. 出现 `[DL] 33%` 或 `[DL] 67%` 时，直接切断开发板电源。
4. 恢复电源，不要按 CLI 键，让 Bootloader 正常启动。

通过判据：日志依次出现 `[UPGRADE] Resuming download...`、`[DL] Resuming from ...`、验签成功、安装到非活动槽和 Trial 启动。旧活动槽在整个下载中从未被擦除。

若服务器不可达，恢复启动会打印恢复下载失败并保留旧槽；重新进入 CLI 后再次执行相同 `upgrade net <URL>` 即可重新开始。

## 2. 安装阶段断电与重做安装

1. 完成普通 OTA，直到看到 `[OK] Firmware verified. Type 'reboot' to install.`。
2. 不要输入 `reboot`，在 CLI 输入 `test power install`，或点上位机的“安装阶段断电保持”。
3. 看到 `[TEST] INSTALL HOLD: SWAPPING persisted...` 后的 15 秒内切断开发板电源。
4. 恢复电源，不进入 CLI。

通过判据：启动时出现 `[UPGRADE] Pending state: 7` 和 `Installing to slot ...`，随后完成安装并 Trial 启动。原因是 `SWAPPING` 在擦除目标槽前已写进内部 Flash journal；重新启动会用完整、已验签的外置下载包重新擦除并写入目标槽，而不是跳转到半写入镜像。

如果没有在 15 秒内断电，安装会继续正常完成；这是预期行为。

## 3. Trial 未确认阶段断电与自动回退

1. 在上位机“掉电恢复验证包”填写一个新版本（例如当前 2.0.0 时填 2.0.1），保持延迟 `15000` ms，点击“构建 Trial 未确认断电包”。
2. 启动服务器，将生成的相反槽包 OTA、验签成功后输入 `reboot`。
3. 出现 `[APP] Trial confirmation delayed for 15000 ms` 后、`[APP] Trial firmware confirmed` 前切断电源。
4. 恢复电源，不进入 CLI。

通过判据：Bootloader 打印 `Trial was not confirmed; reverting to slot ...`，随后跳转到更新前的活动槽。该测试包只改变确认时机；其 ECDSA 包格式、镜像布局和安装路径与普通发布包相同。

## 4. Factory 镜像制备与恢复

Factory 镜像必须为 **Slot-A 包**。Factory 区位于 W25Q64 `0x200000`，普通 OTA 下载缓存位于 `0x000000`，两者互不覆盖。

1. 在上位机“Factory 镜像”中选择已发布的 `*_slotA.pkg`，点击“发送 factory net”。
2. 等待 `[FACTORY] Image ready: v...` 或 `[OK] Factory image provisioned and signature verified.`。
3. 进入 CLI 输入 `factory`。

通过判据：设备打印 `Factory restore OK -> slot A, v...` 并重启，从 `0x08020000` 启动 Slot A。

安全判据：`factory net` 被中断或收到损坏包时，制备命令失败；`factory` 会先验证头 CRC、镜像 CRC、嵌入公钥哈希和 ECDSA 签名，并额外拒绝目标为 Slot-B 的包。因此不会把未完成、损坏或目标错误的镜像写入内部 Flash。

## 建议保留的证据

每项测试保存上位机串口日志，并记录：测试版本、断电点、恢复后的 `info`、`status`、ACTIVE 槽、FW 版本及 Rollbacks 计数。该证据可直接作为项目可靠性验证材料。
