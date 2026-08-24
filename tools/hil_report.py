"""Evidence-based Hardware-in-the-Loop report generation for FlashSafe Pro.

The board remains the source of truth.  This module never simulates an OTA,
power cut, or recovery; it recognizes explicit UART messages produced during a
real hardware session and renders their evidence into a portable Markdown file.
"""

from __future__ import annotations

import datetime as dt


CHECKS = (
    {
        "id": "secure_ota",
        "name": "安全 OTA 验签",
        "required": ("[ECDSA] Firmware signature VALID", "[UPGRADE] Verified OK:"),
        "manual": False,
    },
    {
        "id": "trial_confirm",
        "name": "A/B Trial 安装与确认",
        "required": ("[UPGRADE] Installed trial slot", "[APP] Trial firmware confirmed"),
        "manual": False,
    },
    {
        "id": "download_power_resume",
        "name": "下载阶段掉电恢复",
        "required": ("[UPGRADE] Resuming download", "[OK] Download complete"),
        "manual": True,
    },
    {
        "id": "install_power_resume",
        "name": "安装阶段掉电恢复",
        "required": ("[TEST] INSTALL HOLD", "[UPGRADE] Installing to slot"),
        "manual": True,
    },
    {
        "id": "trial_power_rollback",
        "name": "Trial 未确认自动回退",
        "required": ("[APP] Trial confirmation delayed", "[BOOT] Trial was not confirmed; reverting"),
        "manual": True,
    },
    {
        "id": "factory_provision",
        "name": "Factory 镜像制备与验签",
        "required": ("[FACTORY] Image ready:", "[OK] Factory image provisioned and signature verified."),
        "manual": False,
    },
    {
        "id": "factory_restore",
        "name": "Factory 镜像恢复到 A 槽",
        "required": ("[UPGRADE] Factory restore OK -> slot A",),
        "manual": False,
    },
)


def _normalise_lines(lines):
    normalised = []
    for line in lines or []:
        if isinstance(line, dict):
            timestamp = str(line.get("t", ""))
            message = str(line.get("m", ""))
        else:
            timestamp, message = "", str(line)
        normalised.append({"t": timestamp, "m": message.strip()})
    return normalised


def analyze_hil_log(lines):
    """Return one evidence result per HIL scenario.

    A scenario passes only when every required device message is present.  This
    makes missing human actions (especially a physical power cut) visible as
    "unverified" rather than being reported as a pass.
    """
    normalised = _normalise_lines(lines)
    results = {}
    for check in CHECKS:
        evidence = []
        for token in check["required"]:
            match = next((line for line in normalised if token in line["m"]), None)
            if match:
                evidence.append(match)
        results[check["id"]] = {
            "name": check["name"],
            "manual": check["manual"],
            "passed": len(evidence) == len(check["required"]),
            "required": check["required"],
            "evidence": evidence,
        }
    return results


def render_hil_markdown(analysis, metadata=None):
    """Render a concise, reviewable report without claiming unobserved tests."""
    metadata = metadata or {}
    generated = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()
    lines = [
        "# FlashSafe Pro HIL 测试报告",
        "",
        f"- 生成时间（UTC）：{generated}",
        f"- 开发板：{metadata.get('board', 'STM32F407ZGT6')}",
        f"- 串口：{metadata.get('serial', '未提供')}",
        f"- 活动槽：{metadata.get('active_slot', '未从本次日志识别')}",
        "- 证据来源：上位机本次会话捕获的 MCU UART 日志。",
        "- 说明：物理断电由测试人员执行；报告仅根据设备实际输出判定，不模拟掉电。",
        "",
        "| 场景 | 结果 | 证据 |",
        "| --- | --- | --- |",
    ]
    for check in CHECKS:
        result = analysis.get(check["id"], {})
        status = "通过" if result.get("passed") else "待验证"
        evidence = result.get("evidence", [])
        brief = "<br>".join(
            f"`{entry.get('t', '')} {entry.get('m', '')}`" for entry in evidence
        ) or "未发现所需设备日志"
        lines.append(f"| {check['name']} | {status} | {brief} |")

    lines.extend(["", "## 详细证据", ""])
    for check in CHECKS:
        result = analysis.get(check["id"], {})
        lines.append(f"### {check['name']} — {'通过' if result.get('passed') else '待验证'}")
        if result.get("evidence"):
            lines.append("```text")
            for entry in result["evidence"]:
                prefix = f"[{entry['t']}] " if entry.get("t") else ""
                lines.append(prefix + entry["m"])
            lines.append("```")
        else:
            lines.append("本次串口会话未采集到完整证据。")
        if check["manual"]:
            lines.append("此场景需要人工执行物理断电；缺少恢复日志时不能判定为通过。")
        lines.append("")
    return "\n".join(lines)
