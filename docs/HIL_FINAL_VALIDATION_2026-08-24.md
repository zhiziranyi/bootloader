# FlashSafe Pro 最终实板验证报告

验证日期：2026-08-24  
目标硬件：STM32F407ZGT6 + LAN8720 + W25Q64 + SSD1306  
网络环境：Windows ICS，固件服务器 `192.168.137.1:8000`  
串口：USART1 / COM3 / 115200

本报告来自上位机采集的 MCU UART 实测日志。所有包均经当前嵌入式 ECDSA P-256 公钥验证；物理掉电由人工切断开发板供电完成。

| 场景 | 实测结论 | 关键设备证据 |
|---|---|---|
| 安全 OTA 验签 | 通过 | `Firmware signature VALID`、`Verified OK: v2.0.31` |
| A/B Trial 安装与确认 | 通过 | `Installed trial slot B`、`Trial firmware confirmed` |
| 下载阶段掉电恢复 | 通过 | `Resuming download...`、`Download complete` |
| 安装阶段掉电恢复 | 通过 | `INSTALL HOLD: SWAPPING persisted`、后续重新 `Installing to slot A` |
| Trial 未确认自动回退 | 通过 | `Trial confirmation delayed for 15000 ms`、`Trial was not confirmed; reverting to slot B` |
| Factory 镜像制备 | 通过 | `Factory image ready: v2.0.31`、`Factory image provisioned and signature verified` |
| Factory 恢复 | 通过 | `Factory restore OK -> slot A, v2.0.31` |

## 最终状态

测试结束后 Factory 镜像已恢复到 Slot A。项目具备已验证的安全 OTA、A/B 双槽试运行确认、掉电恢复、自动回退、人工回滚、密钥轮换和受验签保护的 Factory 恢复能力。

运行时生成的原始 Markdown 报告与 `hil-evidence.json` 位于项目根目录的 `reports/`，由 `.gitignore` 保护，避免后续本地测试数据被源码提交覆盖。此文档保留最终可公开的验证摘要。
