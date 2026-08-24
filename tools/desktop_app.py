#!/usr/bin/env python3
"""Native Windows host application for FlashSafe Pro.

It does not open a browser or a web server. The application reuses the
validated local serial and project-operation layer from ``webui.py``.
"""

import os
import re
import shutil
import sys
from pathlib import Path


def find_project_root(cwd=None, executable=None):
    """Find a directory containing platformio.ini from source or a frozen EXE."""
    cwd_path = Path(cwd or Path.cwd()).resolve()
    exe_path = Path(executable or sys.executable).resolve()
    candidates = [cwd_path, *cwd_path.parents, exe_path.parent, *exe_path.parent.parents]
    for candidate in candidates:
        if (candidate / "platformio.ini").is_file():
            return candidate
    raise FileNotFoundError("Cannot find project root containing platformio.ini")


def active_slot_from_logs(logs):
    """Return the most recently reported active A/B slot, if present."""
    for line in reversed(logs or []):
        message = line.get("m", "") if isinstance(line, dict) else str(line)
        match = re.search(r"\bactive\s*=\s*([AB])\b", message, re.IGNORECASE)
        if not match:
            match = re.search(r"\bActive\s+Slot\s*:\s*([AB])\b", message, re.IGNORECASE)
        if match:
            return match.group(1).upper()
    return None


PROJECT_ROOT = find_project_root()
os.environ["FLASHSAFE_PROJECT_ROOT"] = str(PROJECT_ROOT)
if not os.environ.get("FLASHSAFE_PYTHON"):
    os.environ["FLASHSAFE_PYTHON"] = shutil.which("python") or "python"
TOOLS_DIR = PROJECT_ROOT / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import tkinter as tk
from tkinter import messagebox, scrolledtext, ttk

import webui


APP_TITLE = "FlashSafe Pro 上位机"


class FlashSafeDesktopApp:
    def __init__(self, root):
        self.root = root
        self.root.title(APP_TITLE)
        self.root.geometry("1180x810")
        self.root.minsize(940, 650)
        self.serial_port = tk.StringVar(value="COM3")
        self.serial_baud = tk.StringVar(value="115200")
        self.ota_url = tk.StringVar(value="")
        self.ota_host = tk.StringVar(value="192.168.137.1")
        self.ota_package = tk.StringVar(value="")
        self.ota_hint = tk.StringVar(value="等待 Bootloader 启动日志，以自动识别 ACTIVE 槽。")
        self.release_version = tk.StringVar(value=self._latest_version())
        self.fw_port = tk.StringVar(value="8000")
        self.flash_target = tk.StringVar(value="bootloader")
        self.serial_state = tk.StringVar(value="串口：未连接")
        self.fw_state = tk.StringVar(value="固件服务器：未启动")
        self.release_state = tk.StringVar(value="发布：空闲")
        self._device_log_count = 0
        self._task_log_count = 0
        self._active_slot = None
        self._build()
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.refresh()

    @staticmethod
    def _latest_version():
        releases = webui.release_list()
        return releases[0]["version"] if releases else "1.2.3"

    def _build(self):
        style = ttk.Style(self.root)
        style.configure("Header.TLabel", font=("Microsoft YaHei UI", 16, "bold"))
        style.configure("Status.TLabel", foreground="#146c43")
        outer = ttk.Frame(self.root, padding=12)
        outer.pack(fill="both", expand=True)
        ttk.Label(outer, text=APP_TITLE, style="Header.TLabel").pack(anchor="w")
        ttk.Label(outer, text="STM32F407ZGT6 · A/B OTA · ECDSA P-256 · LAN8720").pack(anchor="w", pady=(0, 8))

        states = ttk.Frame(outer)
        states.pack(fill="x", pady=(0, 8))
        ttk.Label(states, textvariable=self.serial_state, style="Status.TLabel").pack(side="left", padx=(0, 24))
        ttk.Label(states, textvariable=self.fw_state, style="Status.TLabel").pack(side="left", padx=(0, 24))
        ttk.Label(states, textvariable=self.release_state).pack(side="left")

        notebook = ttk.Notebook(outer)
        notebook.pack(fill="both", expand=True)
        device_tab = ttk.Frame(notebook, padding=10)
        release_tab = ttk.Frame(notebook, padding=10)
        log_tab = ttk.Frame(notebook, padding=10)
        guide_tab = ttk.Frame(notebook, padding=10)
        notebook.add(device_tab, text="设备与 OTA")
        notebook.add(release_tab, text="发布、服务器与烧录")
        notebook.add(log_tab, text="本机任务日志")
        notebook.add(guide_tab, text="操作流程与记录")
        self._build_device_tab(device_tab)
        self._build_release_tab(release_tab)
        self.task_log = self._make_log(log_tab, "本机任务日志")
        self._build_guide_tab(guide_tab)

    def _section(self, parent, title):
        frame = ttk.LabelFrame(parent, text=title, padding=10)
        frame.pack(fill="x", pady=(0, 10))
        return frame

    def _build_device_tab(self, parent):
        serial_frame = self._section(parent, "1. 设备串口与 CLI")
        ttk.Label(serial_frame, text="端口").grid(row=0, column=0, sticky="w")
        self.port_box = ttk.Combobox(serial_frame, textvariable=self.serial_port, width=20)
        self.port_box.grid(row=0, column=1, padx=6)
        ttk.Label(serial_frame, text="波特率").grid(row=0, column=2, sticky="w")
        ttk.Entry(serial_frame, textvariable=self.serial_baud, width=12).grid(row=0, column=3, padx=6)
        ttk.Button(serial_frame, text="刷新端口", command=self.refresh_ports).grid(row=0, column=4, padx=3)
        ttk.Button(serial_frame, text="连接", command=self.connect_serial).grid(row=0, column=5, padx=3)
        ttk.Button(serial_frame, text="断开", command=self.disconnect_serial).grid(row=0, column=6, padx=3)
        ttk.Button(serial_frame, text="复位后进入 CLI", command=lambda: self.send_command("x")).grid(row=0, column=7, padx=3)
        ttk.Label(serial_frame, text="先关闭友善串口助手；手动复位后，在 Bootloader 的 3 秒窗口点击“复位后进入 CLI”。").grid(row=1, column=0, columnspan=8, sticky="w", pady=(8, 0))

        command_frame = ttk.Frame(serial_frame)
        command_frame.grid(row=2, column=0, columnspan=8, sticky="ew", pady=(8, 0))
        serial_frame.columnconfigure(7, weight=1)
        for command in ("help", "info", "status", "reboot"):
            ttk.Button(command_frame, text=command, command=lambda value=command: self.send_command(value)).pack(side="left", padx=(0, 4))
        self.command = tk.StringVar()
        command_entry = ttk.Entry(command_frame, textvariable=self.command)
        command_entry.pack(side="left", fill="x", expand=True, padx=8)
        command_entry.bind("<Return>", lambda _event: self.send_command())
        ttk.Button(command_frame, text="发送命令", command=self.send_command).pack(side="left")
        self.device_log = self._make_log(parent, "设备串口日志", height=20)

        ota_frame = self._section(parent, "2. OTA 升级")
        ttk.Label(ota_frame, text="服务器 IP").grid(row=0, column=0, sticky="w")
        ttk.Entry(ota_frame, textvariable=self.ota_host, width=17).grid(row=0, column=1, sticky="w", padx=(8, 4))
        ttk.Label(ota_frame, text="包").grid(row=0, column=2, sticky="w")
        self.package_box = ttk.Combobox(ota_frame, textvariable=self.ota_package, width=37, state="readonly")
        self.package_box.grid(row=0, column=3, sticky="ew", padx=(8, 4))
        self.package_box.bind("<<ComboboxSelected>>", lambda _event: self.generate_ota_url())
        ttk.Button(ota_frame, text="生成推荐 URL", command=self.generate_ota_url).grid(row=0, column=4, padx=3)
        ttk.Label(ota_frame, text="包 URL（不带 http://）").grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(ota_frame, textvariable=self.ota_url).grid(row=1, column=1, columnspan=3, sticky="ew", padx=8, pady=(8, 0))
        ttk.Button(ota_frame, text="发送 upgrade net", command=self.send_ota).grid(row=1, column=4, pady=(8, 0))
        ota_frame.columnconfigure(3, weight=1)
        ttk.Label(ota_frame, textvariable=self.ota_hint).grid(row=2, column=0, columnspan=5, sticky="w", pady=(8, 0))

    def _build_release_tab(self, parent):
        release_frame = self._section(parent, "3. 发布签名固件")
        ttk.Label(release_frame, text="版本号").grid(row=0, column=0)
        ttk.Entry(release_frame, textvariable=self.release_version, width=16).grid(row=0, column=1, padx=8)
        ttk.Button(release_frame, text="构建 A/B 并发布", command=self.release).grid(row=0, column=2, padx=3)
        ttk.Button(release_frame, text="轮换密钥并重建", command=self.rotate_keys).grid(row=0, column=3, padx=3)
        ttk.Label(release_frame, text="发布会构建、签名、验签并写入 firmware\\release-vX.Y.Z.json。密钥轮换仅用于量产准备。 ").grid(row=1, column=0, columnspan=4, sticky="w", pady=(8, 0))

        server_frame = self._section(parent, "4. 固件服务器")
        ttk.Label(server_frame, text="端口").grid(row=0, column=0)
        ttk.Entry(server_frame, textvariable=self.fw_port, width=10).grid(row=0, column=1, padx=8)
        ttk.Button(server_frame, text="启动服务器", command=self.start_server).grid(row=0, column=2, padx=3)
        ttk.Button(server_frame, text="停止", command=self.stop_server).grid(row=0, column=3, padx=3)
        ttk.Label(server_frame, text="Windows 网络共享时，LAN8720 通常访问 192.168.137.1:8000。 ").grid(row=1, column=0, columnspan=4, sticky="w", pady=(8, 0))

        flash_frame = self._section(parent, "5. 串口烧录")
        ttk.Label(flash_frame, text="目标").grid(row=0, column=0)
        ttk.Combobox(flash_frame, textvariable=self.flash_target, state="readonly", width=18,
                     values=("bootloader", "app_a", "app_b")).grid(row=0, column=1, padx=8)
        ttk.Button(flash_frame, text="开始串口烧录", command=self.flash).grid(row=0, column=2, padx=3)
        ttk.Label(flash_frame, text="Bootloader 烧录：BOOT0 拉高并复位；完成后 BOOT0 拉低再复位。上位机不会自动改变硬件接线。 ").grid(row=1, column=0, columnspan=3, sticky="w", pady=(8, 0))

        files_frame = self._section(parent, "6. 本地发布文件与密钥")
        ttk.Button(files_frame, text="打开固件目录", command=lambda: self.open_folder(PROJECT_ROOT / "firmware")).grid(row=0, column=0, padx=(0, 6))
        ttk.Button(files_frame, text="打开密钥目录", command=lambda: self.open_folder(TOOLS_DIR / "keys")).grid(row=0, column=1, padx=6)
        ttk.Label(files_frame, text="固件包、release 清单、公钥和私钥均保存在项目内；私钥不得外发。 ").grid(row=1, column=0, columnspan=2, sticky="w", pady=(8, 0))

    def _build_guide_tab(self, parent):
        guide = self._section(parent, "操作流程")
        guide_text = scrolledtext.ScrolledText(guide, height=20, wrap="word",
                                               font=("Microsoft YaHei UI", 10))
        guide_text.pack(fill="both", expand=True)
        guide_text.insert("1.0", """FlashSafe Pro 验证流程

1. 关闭其他串口软件，选择 COM 端口并点击“连接”。
2. 手动复位开发板；在 Bootloader 的 3 秒窗口点击“复位后进入 CLI”。
3. 点击 info 或 status：上位机会从 [CONFIG] active=A/B 自动识别当前活动槽。
4. 在“发布、服务器与烧录”页填写新版本，点击“构建 A/B 并发布”。
5. 点击“启动服务器”。电脑使用网络共享时，服务器 IP 通常是 192.168.137.1。
6. 回到“设备与 OTA”页：软件会自动选择与 ACTIVE 槽相反的最新包，并生成 URL。
7. 点击“发送 upgrade net”，等待下载、CRC、ECDSA 验签均通过。
8. 点击 reboot。设备会安装到非活动槽、试运行并由新 APP 自动确认。
9. 再次进入 CLI 点击 info/status，确认 ACTIVE 已切换、版本正确、状态为 DONE。

提示：不要烧录 Bootloader 作为日常升级方式；日常演示使用“发布 -> OTA -> reboot”。
""")
        guide_text.configure(state="disabled")

        notes = self._section(parent, "实验记录 / 备注（可自由填写）")
        self.notes = scrolledtext.ScrolledText(notes, height=10, wrap="word",
                                               font=("Microsoft YaHei UI", 10))
        self.notes.pack(fill="both", expand=True)
        footer = ttk.Frame(notes)
        footer.pack(fill="x", pady=(6, 0))
        ttk.Label(footer, text="此区域仅保存在当前上位机窗口中；关闭程序前可自行复制保存。 ").pack(side="left")
        ttk.Button(footer, text="清空备注", command=lambda: self.notes.delete("1.0", "end")).pack(side="right")

    def _available_ota_packages(self, status, target_slot):
        packages = []
        for release in status.get("releases", []):
            for package in release.get("packages", []):
                if str(package.get("target_slot", "")).upper() == target_slot:
                    name = package.get("name", "")
                    if name and name not in packages:
                        packages.append(name)
        if packages:
            return packages
        suffix = f"_slot{target_slot}.pkg".lower()
        return [item["name"] for item in status.get("pkgs", [])
                if item.get("name", "").lower().endswith(suffix)]

    def update_ota_recommendation(self, status):
        active_slot = active_slot_from_logs(status.get("serial", {}).get("logs", []))
        if not active_slot:
            self.ota_hint.set("尚未识别 ACTIVE 槽：请复位后进入 CLI，并点击 info 或 status。")
            return
        target_slot = "B" if active_slot == "A" else "A"
        packages = self._available_ota_packages(status, target_slot)
        self.package_box["values"] = packages
        current = self.ota_package.get()
        auto_package = getattr(self, "_auto_package", "")
        if packages and (not current or current == auto_package or current not in packages):
            self.ota_package.set(packages[0])
            self._auto_package = packages[0]
            self.generate_ota_url()
        elif not packages:
            self.ota_package.set("")
        self._active_slot = active_slot
        self.ota_hint.set(
            f"已识别 ACTIVE={active_slot}；已列出目标槽 {target_slot} 的发布包。请选择后点击“发送 upgrade net”。"
        )

    def generate_ota_url(self):
        host = self.ota_host.get().strip().replace("http://", "").replace("https://", "").rstrip("/")
        package = self.ota_package.get().strip()
        port = self.fw_port.get().strip() or "8000"
        if not host or not package:
            self.ota_hint.set("请先进入 CLI 读取 ACTIVE 槽，并确保已发布 A/B 固件包。")
            return
        self.ota_url.set(f"{host}:{port}/{package}")

    @staticmethod
    def _make_log(parent, title, height=16):
        frame = ttk.LabelFrame(parent, text=title, padding=5)
        frame.pack(fill="both", expand=True, pady=(0, 10))
        widget = scrolledtext.ScrolledText(frame, height=height, wrap="word", state="disabled",
                                           font=("Cascadia Mono", 10))
        widget.pack(fill="both", expand=True)
        return widget

    def _append_logs(self, widget, lines, count):
        if len(lines) < count:
            widget.configure(state="normal")
            widget.delete("1.0", "end")
            widget.configure(state="disabled")
            count = 0
        if len(lines) > count:
            widget.configure(state="normal")
            for line in lines[count:]:
                widget.insert("end", f"[{line['t']}] {line['m']}\n")
            widget.see("end")
            widget.configure(state="disabled")
        return len(lines)

    def _operation(self, action):
        try:
            action()
        except (ValueError, RuntimeError, FileNotFoundError) as exc:
            messagebox.showerror(APP_TITLE, str(exc))

    def open_folder(self, folder):
        folder.mkdir(parents=True, exist_ok=True)
        try:
            os.startfile(str(folder))
        except AttributeError:
            messagebox.showinfo(APP_TITLE, str(folder))

    def refresh_ports(self):
        status = webui.serial_status()
        ports = [item["device"] for item in status["ports"]]
        if self.serial_port.get() and self.serial_port.get() not in ports:
            ports.insert(0, self.serial_port.get())
        self.port_box["values"] = ports or ("COM3",)

    def connect_serial(self):
        self._operation(lambda: webui.open_serial(self.serial_port.get(), self.serial_baud.get()))

    def disconnect_serial(self):
        self._operation(webui.close_serial)

    def send_command(self, command=None):
        text = command if command is not None else self.command.get().strip()
        if not text:
            return
        self._operation(lambda: webui.send_serial(text))
        if command is None:
            self.command.set("")

    def send_ota(self):
        url = self.ota_url.get().strip().replace("http://", "").replace("https://", "")
        if not url or "/" not in url:
            messagebox.showwarning(APP_TITLE, "请输入 host:port/firmware.pkg，例如 192.168.137.1:8000/firmware_v1.2.3_slotB.pkg")
            return
        if messagebox.askyesno(APP_TITLE, "确认该包的目标槽与当前 ACTIVE 槽相反？"):
            self._operation(lambda: webui.send_serial(f"upgrade net {url}"))

    def release(self):
        self._operation(lambda: webui.start_release(self.release_version.get()))

    def rotate_keys(self):
        if messagebox.askyesno(APP_TITLE, "轮换密钥会使旧签名包失效。确认重新构建全部固件？"):
            self._operation(lambda: webui.start_rotate(self.release_version.get()))

    def start_server(self):
        self._operation(lambda: webui.start_fw_server(self.fw_port.get()))

    def stop_server(self):
        self._operation(webui.FW_SERVER_RUNNER.stop)

    def flash(self):
        target = self.flash_target.get()
        prompt = "确认 STM32 已进入 ROM 串口下载模式？"
        if target == "bootloader":
            prompt = "确认 BOOT0 已接 3.3V、刚按复位并进入 ROM 串口下载模式？"
        if messagebox.askyesno(APP_TITLE, prompt):
            self._operation(lambda: webui.start_flash(target, self.serial_port.get()))

    def refresh(self):
        try:
            status = webui.status_json()
            serial_status = status["serial"]
            self.serial_state.set("串口：" + (f"{serial_status['port']} @ {serial_status['baud']}" if serial_status["connected"] else "未连接"))
            server_status = status["firmware_server"]
            self.fw_state.set("固件服务器：" + (f"运行中（端口 {server_status['port']}）" if server_status["running"] else "未启动"))
            runner = status["release"]
            self.release_state.set("发布：" + ("构建中" if runner["running"] else "空闲"))
            self.refresh_ports()
            self.update_ota_recommendation(status)
            self._device_log_count = self._append_logs(self.device_log, serial_status["logs"], self._device_log_count)
            self._task_log_count = self._append_logs(self.task_log, status["logs"], self._task_log_count)
        except Exception as exc:
            self.release_state.set(f"状态读取失败：{exc}")
        self.root.after(600, self.refresh)

    def close(self):
        try:
            webui.close_serial()
            webui.FW_SERVER_RUNNER.stop()
        finally:
            self.root.destroy()


def main():
    root = tk.Tk()
    FlashSafeDesktopApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
