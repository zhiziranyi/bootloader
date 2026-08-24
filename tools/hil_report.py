"""Evidence-based Hardware-in-the-Loop report generation for FlashSafe Pro.

The board remains the source of truth.  This module never simulates an OTA,
power cut, or recovery; it recognizes explicit UART messages produced during a
real hardware session and renders their evidence into a portable Markdown file.
"""

from __future__ import annotations

import datetime as dt
import json
from pathlib import Path


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

EVIDENCE_SCHEMA_VERSION = 1


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


def _empty_evidence_store():
    return {"schema_version": EVIDENCE_SCHEMA_VERSION, "checks": {}}


def _record_is_complete(check, record):
    if not isinstance(record, dict) or not isinstance(record.get("evidence"), list):
        return False
    messages = [str(entry.get("m", "")) for entry in record["evidence"]
                if isinstance(entry, dict)]
    return all(any(token in message for message in messages) for token in check["required"])


def _normalise_store(store):
    normalised = _empty_evidence_store()
    if not isinstance(store, dict) or not isinstance(store.get("checks"), dict):
        return normalised
    records = store["checks"]
    for check in CHECKS:
        record = records.get(check["id"])
        if _record_is_complete(check, record):
            normalised["checks"][check["id"]] = {
                "recorded_at": str(record.get("recorded_at", "")),
                "evidence": _normalise_lines(record["evidence"]),
            }
    return normalised


def load_evidence(path):
    """Load only complete, structurally valid evidence records from disk."""
    path = Path(path)
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError, TypeError):
        return _empty_evidence_store()
    return _normalise_store(raw)


def save_evidence(path, store):
    """Atomically save the evidence ledger so a host crash cannot corrupt it."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    normalised = _normalise_store(store)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(normalised, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def record_completed_evidence(store, analysis, recorded_at=None):
    """Add complete scenarios to an append-safe evidence ledger.

    A previously stored passing record remains untouched.  This avoids turning
    a later partial or unrelated session into a new claimed result.
    """
    saved = _normalise_store(store)
    changed = False
    timestamp = recorded_at or dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()
    for check in CHECKS:
        check_id = check["id"]
        result = analysis.get(check_id, {}) if isinstance(analysis, dict) else {}
        if not result.get("passed") or check_id in saved["checks"]:
            continue
        evidence = _normalise_lines(result.get("evidence", []))
        candidate = {"recorded_at": timestamp, "evidence": evidence}
        if _record_is_complete(check, candidate):
            saved["checks"][check_id] = candidate
            changed = True
    return saved, changed


def _legacy_evidence_lines(markdown, check):
    """Extract the device-log code block below one passed legacy heading."""
    heading = f"### {check['name']} — 通过"
    start = markdown.find(heading)
    if start < 0:
        return []
    next_heading = markdown.find("\n### ", start + len(heading))
    section = markdown[start:next_heading if next_heading >= 0 else len(markdown)]
    fence_start = section.find("```text")
    if fence_start < 0:
        return []
    content_start = section.find("\n", fence_start)
    fence_end = section.find("\n```", content_start + 1)
    if content_start < 0 or fence_end < 0:
        return []
    lines = []
    for raw in section[content_start + 1:fence_end].splitlines():
        timestamp, message = "", raw.strip()
        if raw.startswith("[") and "] " in raw:
            timestamp, message = raw[1:].split("] ", 1)
        lines.append({"t": timestamp, "m": message})
    return _normalise_lines(lines)


def import_legacy_reports(report_directory, store):
    """Migrate only fully evidenced passes from old exported Markdown reports."""
    saved = _normalise_store(store)
    directory = Path(report_directory)
    if not directory.is_dir():
        return saved, False
    changed = False
    for report_path in sorted(directory.glob("hil-report-*.md"), reverse=True):
        try:
            markdown = report_path.read_text(encoding="utf-8")
        except OSError:
            continue
        for check in CHECKS:
            check_id = check["id"]
            if check_id in saved["checks"]:
                continue
            candidate = {
                "recorded_at": f"迁移自 {report_path.name}",
                "evidence": _legacy_evidence_lines(markdown, check),
            }
            if _record_is_complete(check, candidate):
                saved["checks"][check_id] = candidate
                changed = True
    return saved, changed


def combine_with_saved_evidence(live_analysis, store):
    """Prefer current-session evidence, otherwise expose verified history."""
    saved = _normalise_store(store)
    combined = {}
    for check in CHECKS:
        check_id = check["id"]
        live = dict((live_analysis or {}).get(check_id, {}))
        if live.get("passed"):
            live["source"] = "本次会话"
            live["recorded_at"] = ""
            combined[check_id] = live
            continue
        historical = saved["checks"].get(check_id)
        if historical and _record_is_complete(check, historical):
            combined[check_id] = {
                "name": check["name"],
                "manual": check["manual"],
                "passed": True,
                "required": check["required"],
                "evidence": historical["evidence"],
                "source": "历史保存证据",
                "recorded_at": historical["recorded_at"],
            }
            continue
        live.setdefault("name", check["name"])
        live.setdefault("manual", check["manual"])
        live.setdefault("passed", False)
        live.setdefault("required", check["required"])
        live.setdefault("evidence", [])
        live["source"] = "待验证"
        live["recorded_at"] = ""
        combined[check_id] = live
    return combined


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
        "- 证据来源：上位机本次会话或已保存的 MCU UART 证据台账。",
        "- 说明：物理断电由测试人员执行；报告仅根据设备实际输出判定，不模拟掉电。",
        "",
        "| 场景 | 结果 | 来源 | 证据 |",
        "| --- | --- | --- | --- |",
    ]
    for check in CHECKS:
        result = analysis.get(check["id"], {})
        status = "通过" if result.get("passed") else "待验证"
        evidence = result.get("evidence", [])
        brief = "<br>".join(
            f"`{entry.get('t', '')} {entry.get('m', '')}`" for entry in evidence
        ) or "未发现所需设备日志"
        source = result.get("source", "本次会话" if result.get("passed") else "待验证")
        lines.append(f"| {check['name']} | {status} | {source} | {brief} |")

    lines.extend(["", "## 详细证据", ""])
    for check in CHECKS:
        result = analysis.get(check["id"], {})
        source = result.get("source", "本次会话" if result.get("passed") else "待验证")
        lines.append(f"### {check['name']} — {'通过' if result.get('passed') else '待验证'}（{source}）")
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
