#!/usr/bin/env python3
"""FlashSafe Pro localhost host workbench.

The service owns local serial ports and project tools, while ``html/`` remains
an ordinary browser front end. It listens only on localhost; the separately
managed firmware server is the component exposed to the board.
"""

import hashlib
import json
import mimetypes
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None


def resolve_project_root():
    """Locate the project for both source and PyInstaller desktop launches."""
    configured = os.environ.get("FLASHSAFE_PROJECT_ROOT", "").strip()
    candidates = []
    if configured:
        candidates.append(Path(configured))
    candidates.extend([Path.cwd(), Path(__file__).resolve().parent.parent])
    for candidate in candidates:
        candidate = candidate.resolve()
        if (candidate / "platformio.ini").is_file():
            return candidate
    return Path(__file__).resolve().parent.parent


PROJECT_ROOT = resolve_project_root()
TOOLS_DIR = PROJECT_ROOT / "tools"
HTML_DIR = PROJECT_ROOT / "html"
KEYS_DIR = TOOLS_DIR / "keys"
BACKUPS_DIR = KEYS_DIR / "backups"
INCLUDE_DIR = PROJECT_ROOT / "include"
PUBKEY_HEADER = INCLUDE_DIR / "public_key.h"
PKG_DIR = PROJECT_ROOT / "firmware"
HOST = "127.0.0.1"
PORT = 8765
LOG_LIMIT = 1000
SERIAL_LOG_LIMIT = 1200

_log_lock = threading.Lock()
_log_lines = []
_serial_log_lock = threading.Lock()
_serial_lines = []
_serial_lock = threading.Lock()
_serial_write_lock = threading.Lock()
_serial = None
_serial_port = None
_serial_baud = None
_serial_error = None
_serial_stop = threading.Event()


def log(msg):
    ts = time.strftime("%H:%M:%S")
    with _log_lock:
        _log_lines.append((ts, str(msg)))
        if len(_log_lines) > LOG_LIMIT:
            del _log_lines[:-LOG_LIMIT]


def log_tail(n=250):
    with _log_lock:
        return [{"t": t, "m": m} for t, m in _log_lines[-n:]]


def serial_log(msg):
    ts = time.strftime("%H:%M:%S")
    with _serial_log_lock:
        _serial_lines.append((ts, str(msg)))
        if len(_serial_lines) > SERIAL_LOG_LIMIT:
            del _serial_lines[:-SERIAL_LOG_LIMIT]


def serial_tail(n=300):
    with _serial_log_lock:
        return [{"t": t, "m": m} for t, m in _serial_lines[-n:]]


def validate_serial_port(port):
    """Return a safe Windows COM port spelling for a host operation."""
    port = str(port or "").strip().upper()
    if not re.fullmatch(r"COM[1-9][0-9]*", port):
        raise ValueError("serial port must be COM followed by a positive number")
    return port


def validate_baud(baud):
    try:
        baud = int(baud)
    except (TypeError, ValueError) as exc:
        raise ValueError("baud must be an integer") from exc
    if baud < 1200 or baud > 2_000_000:
        raise ValueError("baud must be between 1200 and 2000000")
    return baud


def validate_version(version):
    version = str(version or "").strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("version must be X.Y.Z")
    return version


def validate_tcp_port(port):
    try:
        port = int(port)
    except (TypeError, ValueError) as exc:
        raise ValueError("TCP port must be an integer") from exc
    if not 1 <= port <= 65535:
        raise ValueError("TCP port must be between 1 and 65535")
    return port


def validate_trial_confirm_delay(delay_ms):
    """Accept normal releases (0) or a usable physical power-cut window."""
    try:
        delay_ms = int(delay_ms)
    except (TypeError, ValueError) as exc:
        raise ValueError("trial confirmation delay must be an integer") from exc
    if delay_ms != 0 and not 5_000 <= delay_ms <= 60_000:
        raise ValueError("trial confirmation delay must be 0 or between 5000 and 60000 ms")
    return delay_ms


def tool_python_executable():
    """Interpreter for child tool scripts, including from frozen desktop EXE."""
    configured = os.environ.get("FLASHSAFE_PYTHON", "").strip()
    if configured:
        return configured
    if not getattr(sys, "frozen", False):
        return sys.executable
    return shutil.which("python") or "python"


def upload_environment(target):
    environments = {"bootloader": "black_f407zg", "app_a": "app_a", "app_b": "app_b"}
    try:
        return environments[str(target)]
    except KeyError as exc:
        raise ValueError("target must be bootloader, app_a or app_b") from exc


class ManagedProcess:
    """One named, line-logged local child process."""

    def __init__(self, name):
        self.name = name
        self._lock = threading.Lock()
        self._proc = None
        self._exit_code = None
        self._command = []

    def status(self):
        with self._lock:
            running = self._proc is not None and self._proc.poll() is None
            return {"running": running, "exit_code": None if running else self._exit_code,
                    "command": self._command}

    def start(self, command, cwd=PROJECT_ROOT):
        with self._lock:
            if self._proc is not None and self._proc.poll() is None:
                return False, f"{self.name} is already running"
            self._exit_code = None
            self._command = [str(item) for item in command]
            self._proc = subprocess.Popen(
                self._command, cwd=str(cwd), stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                errors="replace", bufsize=1)
            proc = self._proc
        log(f"[{self.name}] started: {' '.join(self._command)}")
        threading.Thread(target=self._pump, args=(proc,), daemon=True).start()
        threading.Thread(target=self._wait, args=(proc,), daemon=True).start()
        return True, "started"

    def _pump(self, proc):
        if proc.stdout is None:
            return
        for raw in iter(proc.stdout.readline, ""):
            log(f"[{self.name}] {raw.rstrip()}")

    def _wait(self, proc):
        code = proc.wait()
        with self._lock:
            if self._proc is proc:
                self._exit_code = code
        log(f"[{self.name}] finished, exit code = {code}")

    def stop(self):
        with self._lock:
            proc = self._proc
        if proc is None or proc.poll() is not None:
            return False, f"{self.name} is not running"
        proc.terminate()
        return True, "stopping"


ROTATE_RUNNER = ManagedProcess("key-rotate")
RELEASE_RUNNER = ManagedProcess("release")
FLASH_RUNNER = ManagedProcess("flash")
FW_SERVER_RUNNER = ManagedProcess("firmware-server")
_fw_server_port = None


def pubkey_sha256():
    try:
        content = PUBKEY_HEADER.read_text(encoding="utf-8")
        match = re.search(r"PUBLIC_KEY\[64\]\s*=\s*\{([^}]+)\}", content)
        if not match:
            return None
        values = re.findall(r"0x([0-9A-Fa-f]{2})", match.group(1))
        if len(values) != 64:
            return None
        return hashlib.sha256(bytes(int(v, 16) for v in values)).hexdigest()
    except OSError:
        return None


def pkg_list():
    if not PKG_DIR.exists():
        return []
    return [{"name": path.name, "size": path.stat().st_size,
             "mtime": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(path.stat().st_mtime))}
            for path in sorted(PKG_DIR.glob("*.pkg"), reverse=True)]


def release_list():
    if not PKG_DIR.exists():
        return []
    releases = []
    for path in PKG_DIR.glob("release-v*.json"):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            version = validate_version(data["version"])
            packages = data.get("packages", [])
            if isinstance(packages, list):
                releases.append({"name": path.name, "version": version,
                                 "generated_utc": data.get("generated_utc", ""),
                                 "packages": packages})
        except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError):
            continue
    releases.sort(key=lambda item: tuple(int(part) for part in item["version"].split(".")),
                  reverse=True)
    return releases


def backup_list():
    return sorted(path.name for path in BACKUPS_DIR.glob("public_key.h.bak-*")) if BACKUPS_DIR.exists() else []


def serial_ports():
    if list_ports is None:
        return []
    return [{"device": item.device, "description": item.description, "hwid": item.hwid}
            for item in list_ports.comports()]


def serial_status():
    with _serial_lock:
        connected = _serial is not None and _serial.is_open
        return {"available": serial is not None, "connected": connected,
                "port": _serial_port, "baud": _serial_baud, "error": _serial_error,
                "ports": serial_ports(), "logs": serial_tail()}


def _serial_reader():
    global _serial_error
    while not _serial_stop.is_set():
        with _serial_lock:
            device = _serial
        if device is None:
            break
        try:
            data = device.readline()
            if data:
                serial_log(data.decode("utf-8", errors="replace").rstrip("\r\n"))
        except Exception as exc:
            with _serial_lock:
                if _serial is device:
                    _serial_error = str(exc)
            serial_log(f"[serial error] {exc}")
            break


def open_serial(port, baud):
    global _serial, _serial_port, _serial_baud, _serial_error
    if serial is None:
        raise RuntimeError("PySerial is unavailable; run: python -m pip install pyserial")
    port, baud = validate_serial_port(port), validate_baud(baud)
    close_serial()
    try:
        device = serial.Serial(port=port, baudrate=baud, timeout=0.2, write_timeout=1)
    except Exception as exc:
        raise RuntimeError(f"cannot open {port}: {exc}") from exc
    with _serial_lock:
        _serial, _serial_port, _serial_baud, _serial_error = device, port, baud, None
        _serial_stop.clear()
    serial_log(f"[serial] connected {port} @ {baud}")
    threading.Thread(target=_serial_reader, daemon=True).start()
    return serial_status()


def close_serial():
    global _serial, _serial_port, _serial_baud
    _serial_stop.set()
    with _serial_lock:
        device, _serial, _serial_port, _serial_baud = _serial, None, None, None
    if device is not None:
        try:
            device.close()
        except Exception:
            pass
        serial_log("[serial] disconnected")
    return serial_status()


SERIAL_COMMAND_INTERBYTE_DELAY_S = 0.003


def write_serial_command(device, text, append_newline=True):
    """Transmit a command slowly enough for the one-byte STM32 IRQ receiver.

    The target uses interrupt-driven, one-byte UART reception during its early
    boot CLI window.  USB-to-TTL adapters may otherwise deliver an entire long
    command in one burst around a reset.  The small 3 ms gap costs less than
    200 ms for a URL, but gives every character an unambiguous receive slot.
    """
    payload = text.rstrip("\r\n") + "\r" if append_newline else text
    encoded = payload.encode("utf-8")
    for index, byte in enumerate(encoded):
        device.write(bytes((byte,)))
        if index + 1 < len(encoded):
            time.sleep(SERIAL_COMMAND_INTERBYTE_DELAY_S)
    device.flush()
    return len(encoded)


def send_serial(text, append_newline=True):
    if not isinstance(text, str) or not text.strip():
        raise ValueError("command text is required")
    if len(text) > 2048:
        raise ValueError("command is too long")
    payload = text.rstrip("\r\n") + ("\r" if append_newline else "")
    with _serial_lock:
        device = _serial
    if device is None or not device.is_open:
        raise RuntimeError("serial port is not connected")
    try:
        with _serial_write_lock:
            count = write_serial_command(device, payload, append_newline=False)
    except Exception as exc:
        raise RuntimeError(f"serial write failed: {exc}") from exc
    serial_log(f"> {text.rstrip()}  [paced {count} bytes]")


def platformio_executable():
    try:
        from release_firmware import platformio_executable as find_platformio
        return find_platformio()
    except (ImportError, FileNotFoundError) as exc:
        raise RuntimeError("PlatformIO executable not found") from exc


def start_rotate(version):
    return ROTATE_RUNNER.start([tool_python_executable(), str(TOOLS_DIR / "regenerate_keys.py"),
                                "--yes", "--version", validate_version(version)])


def start_release(version, trial_confirm_delay_ms=0):
    trial_confirm_delay_ms = validate_trial_confirm_delay(trial_confirm_delay_ms)
    command = [tool_python_executable(), str(TOOLS_DIR / "release_firmware.py"),
               "--version", validate_version(version)]
    if trial_confirm_delay_ms:
        command.extend(["--trial-confirm-delay-ms", str(trial_confirm_delay_ms)])
    return RELEASE_RUNNER.start(command)


def start_fw_server(port):
    global _fw_server_port
    port = validate_tcp_port(port)
    ok, message = FW_SERVER_RUNNER.start([tool_python_executable(), str(TOOLS_DIR / "fw_server.py"),
                                          "--dir", str(PKG_DIR), "--port", str(port)])
    if ok:
        _fw_server_port = port
    return ok, message


def start_flash(target, port):
    env, port = upload_environment(target), validate_serial_port(port)
    command = [platformio_executable(), "run"]
    if target != "bootloader":
        command.extend(["-d", str(PROJECT_ROOT / "app")])
    command.extend(["-e", env, "-t", "upload", "--upload-port", port])
    return FLASH_RUNNER.start(command)


def status_json():
    firmware_server = FW_SERVER_RUNNER.status()
    firmware_server["port"] = _fw_server_port
    rotation = ROTATE_RUNNER.status()
    return {"running": rotation["running"], "exit_code": rotation["exit_code"],
            "key_sha256": pubkey_sha256(), "key_file": str(PUBKEY_HEADER),
            "pkgs": pkg_list(), "releases": release_list(), "backups": backup_list(),
            "logs": log_tail(), "serial": serial_status(),
            "firmware_server": firmware_server, "release": RELEASE_RUNNER.status(),
            "flash": FLASH_RUNNER.status()}


class Handler(BaseHTTPRequestHandler):
    server_version = "FlashSafeHostWorkbench/1.0"

    def log_message(self, fmt, *args):
        log(f"[http] {self.address_string()} - {fmt % args}")

    def _cors_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def _json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self._cors_headers()
        self.end_headers()
        self.wfile.write(body)

    def _download(self, path, name):
        path = Path(path).resolve()
        if not path.is_file():
            self.send_error(404, "File not found")
            return
        body = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Disposition", f'attachment; filename="{name}"')
        self.send_header("Content-Length", str(len(body)))
        self._cors_headers()
        self.end_headers()
        self.wfile.write(body)

    def _payload(self):
        length = int(self.headers.get("Content-Length", 0))
        if length < 0 or length > 8192:
            raise ValueError("request body is too large")
        payload = json.loads(self.rfile.read(length) or b"{}")
        if not isinstance(payload, dict):
            raise ValueError("request body must be an object")
        return payload

    def do_OPTIONS(self):
        self.send_response(204)
        self._cors_headers()
        self.send_header("Access-Control-Max-Age", "600")
        self.end_headers()

    def do_GET(self):
        parsed, route = urlparse(self.path), urlparse(self.path).path
        query = parse_qs(parsed.query)
        if route == "/api/status":
            self._json(status_json())
            return
        if route == "/api/download/pkg":
            name = os.path.basename(query.get("name", [""])[0])
            target = PKG_DIR / name
            if name and target.is_file(): self._download(target, name)
            else: self.send_error(404, "Package not found")
            return
        if route == "/api/download/release":
            name = os.path.basename(query.get("name", [""])[0])
            target = PKG_DIR / name
            if name.startswith("release-v") and target.suffix == ".json" and target.is_file(): self._download(target, name)
            else: self.send_error(404, "Release manifest not found")
            return
        if route == "/api/download/key":
            self._download(PUBKEY_HEADER, "public_key.h")
            return
        if route == "/api/download/private":
            if query.get("confirm") != ["1"]:
                self._json({"error": "confirmation required"}, 403)
            else:
                self._download(KEYS_DIR / "private_key.pem", "private_key.pem")
            return
        relative = "index.html" if route in ("/", "/index.html") else unquote(route.lstrip("/"))
        path = (HTML_DIR / relative).resolve()
        try:
            path.relative_to(HTML_DIR.resolve())
        except ValueError:
            self.send_error(403, "Forbidden")
            return
        if not path.is_file():
            self.send_error(404, "Not found")
            return
        body = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", mimetypes.guess_type(str(path))[0] or "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self._cors_headers()
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        route = urlparse(self.path).path
        try:
            payload = self._payload()
            if route == "/api/rotate":
                ok, message = start_rotate(payload.get("version", ""))
            elif route == "/api/release":
                ok, message = start_release(payload.get("version", ""))
            elif route == "/api/serial/open":
                self._json({"ok": True, "serial": open_serial(payload.get("port"), payload.get("baud", 115200))})
                return
            elif route == "/api/serial/close":
                self._json({"ok": True, "serial": close_serial()})
                return
            elif route == "/api/serial/send":
                send_serial(payload.get("text"), bool(payload.get("newline", True)))
                self._json({"ok": True})
                return
            elif route == "/api/fw-server/start":
                ok, message = start_fw_server(payload.get("port", 8000))
            elif route == "/api/fw-server/stop":
                ok, message = FW_SERVER_RUNNER.stop()
            elif route == "/api/flash":
                ok, message = start_flash(payload.get("target"), payload.get("port"))
            else:
                self.send_error(404, "Not found")
                return
            self._json({"ok": ok, "message": message}, 202 if ok else 409)
        except (ValueError, RuntimeError, TypeError) as exc:
            self._json({"error": str(exc)}, 400)


def main():
    import argparse
    parser = argparse.ArgumentParser(description="FlashSafe Pro localhost host workbench")
    parser.add_argument("--port", "-p", type=int, default=PORT)
    args = parser.parse_args()
    server = ThreadingHTTPServer((HOST, validate_tcp_port(args.port)), Handler)
    print("=" * 56)
    print("  FlashSafe Pro Local Host Workbench")
    print(f"  Open in your browser: http://{HOST}:{args.port}")
    print("  Press Ctrl+C to stop.")
    print("=" * 56)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        close_serial()
        FW_SERVER_RUNNER.stop()
        server.server_close()


if __name__ == "__main__":
    main()
