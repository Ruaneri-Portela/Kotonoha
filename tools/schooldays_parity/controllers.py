"""Local controllers. Importing this module never starts either game."""
from __future__ import annotations

import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import threading
import time

from PIL import Image
from .compare import frame_metrics

try:
    from windows_cap import WindowCapture
except ImportError:
    WindowCapture = None

ROOT = Path(__file__).resolve().parents[2]
DEV_EXE = ROOT / "build/school-days-dev-control4/games/school-days/Release/SchoolDays.exe"
ORIGINAL_EXE = Path(r"D:\SCHOOL DAYS HQ\Overflow\SCHOOLDAYS HQ\SCHOOLDAYS HQ.exe")
OUTPUT = ROOT / "build/school-days-parity-captures"
PROTOCOL_VERSION = 1
SERVER_VERSION = "0.2.0"


def _label(text: str) -> str:
    value = "".join(c for c in text if c.isalnum() or c in "-_")[:64]
    return value or "frame"


def _png(image: Image.Image, label: str, *, directory: Path = OUTPUT,
         captured_ns: int | None = None, backend: str = "unknown",
         extra: dict | None = None) -> dict:
    directory.mkdir(parents=True, exist_ok=True)
    stamp = captured_ns if captured_ns is not None else time.monotonic_ns()
    path = directory / f"{_label(label)}-{stamp}.png"
    image.convert("RGB").save(path)
    metadata = frame_metrics(path)
    metadata.update({"capturedMonotonicNs": stamp,
                     "savedWallClockUtc": datetime.now(timezone.utc).isoformat(),
                     "captureBackend": backend})
    if extra:
        metadata.update(extra)
    sidecar = path.with_suffix(".json")
    sidecar.write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")
    metadata["metadataPath"] = str(sidecar)
    return metadata


class KotonohaController:
    def __init__(self, port: int = 47321):
        self.port = port
        self.process: subprocess.Popen | None = None
        self.connection: socket.socket | None = None
        self.next_id = 0
        self.protocol_version: int | None = None

    def start(self) -> dict:
        if self.process and self.process.poll() is None:
            return self.status()
        if not DEV_EXE.is_file():
            raise FileNotFoundError(f"Build DEV missing: {DEV_EXE}")
        env = os.environ.copy()
        env["KOTONOHA_DEV_PORT"] = str(self.port)
        # Use the same retail Packs as the original, while keeping the DEV
        # executable and its writable profile in the isolated DEV build tree.
        self.process = subprocess.Popen([str(DEV_EXE), "--school-days",
                                         str(ORIGINAL_EXE.parent)],
                                        cwd=DEV_EXE.parent, env=env)
        try:
            deadline = time.monotonic() + 25
            while time.monotonic() < deadline:
                if self.process.poll() is not None:
                    raise RuntimeError(f"Kotonoha DEV exited with {self.process.returncode}")
                try:
                    self.connect()
                    return self.status()
                except OSError:
                    time.sleep(0.1)
            raise TimeoutError("Kotonoha DEV bridge did not listen on loopback")
        except Exception:
            self.stop()
            raise

    def connect(self) -> None:
        if self.connection:
            return
        self.connection = socket.create_connection(("127.0.0.1", self.port), timeout=2)
        self.connection.settimeout(5)
        try:
            hello = self._exchange("hello", {"protocolVersion": PROTOCOL_VERSION})
            self.protocol_version = hello.get("protocolVersion")
            if self.protocol_version != PROTOCOL_VERSION:
                raise RuntimeError(f"DEV protocol mismatch: expected {PROTOCOL_VERSION}, got {self.protocol_version}")
            if self.process and self.process.poll() is None and hello.get("pid") != self.process.pid:
                raise RuntimeError("DEV bridge belongs to a different Kotonoha process")
        except Exception:
            self.connection.close()
            self.connection = None
            raise

    def stop(self) -> dict:
        if self.connection:
            self.connection.close()
            self.connection = None
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.process = None
        self.protocol_version = None
        return {"stopped": True}

    def _exchange(self, method: str, params: dict) -> dict:
        assert self.connection is not None
        self.next_id += 1
        self.connection.sendall((json.dumps({"id": self.next_id, "method": method,
                                              "params": params}) + "\n").encode())
        data = bytearray()
        while not data.endswith(b"\n"):
            chunk = self.connection.recv(65536)
            if not chunk:
                self.connection = None
                raise ConnectionError("Kotonoha DEV bridge disconnected")
            data.extend(chunk)
            if len(data) > 1_000_000:
                raise ValueError("DEV response exceeds 1 MiB")
        response = json.loads(data)
        if not response.get("ok"):
            raise RuntimeError(response.get("error", "DEV call failed"))
        return response["result"]

    def call(self, method: str, **params) -> dict:
        self.connect()
        return self._exchange(method, params)

    def status(self) -> dict:
        try:
            state = self.call("status")
            state["running"] = True
            return state
        except OSError:
            return {"running": False, "port": self.port}

    def health(self) -> dict:
        diagnostic = {"serverVersion": SERVER_VERSION,
                      "expectedProtocolVersion": PROTOCOL_VERSION,
                      "devExecutablePresent": DEV_EXE.is_file(),
                      "transport": "127.0.0.1:" + str(self.port),
                      "captureBackend": "SDL pre-Present readback -> PNG"}
        try:
            state = self.status()
            diagnostic.update({"transportConnected": bool(state.get("running")),
                               "gameConnected": bool(state.get("running")),
                               "windowDetected": "window" in state,
                               "bridgeProtocolVersion": state.get("protocolVersion"),
                               "protocolCompatible": state.get("protocolVersion") == PROTOCOL_VERSION})
        except Exception as error:
            diagnostic.update({"transportConnected": False, "gameConnected": False,
                               "windowDetected": False, "protocolCompatible": False,
                               "error": str(error)})
        return diagnostic

    def click(self, x: float, y: float, action_id: int = 0) -> dict:
        if not 0 <= x <= 1 or not 0 <= y <= 1:
            raise ValueError("Coordinates must be normalized to 0..1")
        window = self.status()["window"]
        return self.call("click", x=round(x * window["width"]),
                         y=round(y * window["height"]), actionId=action_id)

    def move(self, x: float, y: float) -> dict:
        if not 0 <= x <= 1 or not 0 <= y <= 1:
            raise ValueError("Coordinates must be normalized to 0..1")
        window = self.status()["window"]
        return self.call("mouse_move", x=round(x * window["width"]),
                         y=round(y * window["height"]))

    def key(self, key: str, action_id: int = 0) -> dict:
        self.call("key_down", key=key, actionId=action_id)
        return self.call("key_up", key=key, actionId=action_id)

    def capture(self, label: str = "kotonoha", directory: Path = OUTPUT) -> dict:
        before = time.monotonic_ns()
        result = self.call("capture_frame")
        after = time.monotonic_ns()
        intermediate = Path(result["path"]).resolve()
        expected = (DEV_EXE.parent / "dev-captures").resolve()
        if intermediate.parent != expected or intermediate.suffix.lower() != ".bmp":
            raise ValueError("DEV bridge returned a capture outside its fixed directory")
        engine_state = {key: value for key, value in result.items() if key != "path"}
        try:
            with Image.open(intermediate) as image:
                output = _png(image, label, directory=directory,
                              captured_ns=(before + after) // 2,
                              backend="SDL pre-Present",
                              extra={"engine": engine_state,
                                     "timestampUncertaintyNs": (after - before) // 2})
        finally:
            intermediate.unlink(missing_ok=True)
        output["state"] = engine_state
        return output


class OriginalController:
    """External x64 Win32 controller; WGC with visible GDI fallback."""
    def __init__(self):
        if os.name != "nt":
            raise OSError("Original controller requires Windows")
        self.process: subprocess.Popen | None = None
        self.user32 = ctypes.windll.user32
        self.gdi32 = ctypes.windll.gdi32
        self.user32.SetProcessDPIAware()
        self.user32.GetDC.restype = ctypes.c_void_p
        self.user32.GetForegroundWindow.restype = ctypes.c_void_p
        self.user32.GetDC.argtypes = [ctypes.c_void_p]
        self.user32.ReleaseDC.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        self.user32.IsWindow.argtypes = [ctypes.c_void_p]
        self.user32.IsWindowVisible.argtypes = [ctypes.c_void_p]
        self.user32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p,
                                                         ctypes.POINTER(wintypes.DWORD)]
        self.user32.GetClientRect.argtypes = [ctypes.c_void_p, ctypes.POINTER(wintypes.RECT)]
        self.user32.ClientToScreen.argtypes = [ctypes.c_void_p, ctypes.POINTER(wintypes.POINT)]
        self.user32.ShowWindow.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.user32.SetForegroundWindow.argtypes = [ctypes.c_void_p]
        self.user32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
        self.user32.GetWindowTextW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]
        self.user32.GetClassNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]
        self.user32.mouse_event.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint,
                                            ctypes.c_uint, ctypes.c_size_t]
        self.user32.keybd_event.argtypes = [ctypes.c_ubyte, ctypes.c_ubyte,
                                            ctypes.c_uint, ctypes.c_size_t]
        self.gdi32.CreateCompatibleDC.restype = ctypes.c_void_p
        self.gdi32.CreateCompatibleDC.argtypes = [ctypes.c_void_p]
        self.gdi32.CreateCompatibleBitmap.restype = ctypes.c_void_p
        self.gdi32.CreateCompatibleBitmap.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        self.gdi32.SelectObject.restype = ctypes.c_void_p
        self.gdi32.SelectObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        self.gdi32.BitBlt.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                     ctypes.c_int, ctypes.c_int, ctypes.c_void_p,
                                     ctypes.c_int, ctypes.c_int, ctypes.c_uint]
        self.gdi32.GetDIBits.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                         ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p,
                                         ctypes.c_void_p, ctypes.c_uint]
        self.gdi32.DeleteObject.argtypes = [ctypes.c_void_p]
        self.gdi32.DeleteDC.argtypes = [ctypes.c_void_p]
        self.hwnd = 0
        self.capture_backend = "GDI"
        self.capture_fallback_reason = "WGC not initialized"
        self._wgc = None
        self._wgc_latest = None
        self._wgc_condition = threading.Condition()
        self.actions: list[dict] = []

    def _initialize_wgc(self) -> None:
        if WindowCapture is None:
            self.capture_fallback_reason = "windows-cap is unavailable in this Python environment"
            return
        title = ctypes.create_unicode_buffer(512)
        klass = ctypes.create_unicode_buffer(512)
        self.user32.GetWindowTextW(self.hwnd, title, len(title))
        self.user32.GetClassNameW(self.hwnd, klass, len(klass))
        if not title.value:
            self.capture_fallback_reason = "Original window has no title for WGC selection"
            return
        try:
            capture = WindowCapture(title.value, klass.value or None)
        except Exception as error:
            self.capture_fallback_reason = f"WGC initialization failed: {error}"
            return
        self._wgc = capture
        self.capture_backend = "WGC"
        self.capture_fallback_reason = ""
        def collect() -> None:
            while self._wgc is capture:
                try:
                    data = capture.next()
                    width, height = capture.client_size
                    if width <= 0 or height <= 0 or len(data) != width * height * 4:
                        continue
                    with self._wgc_condition:
                        self._wgc_latest = (time.monotonic_ns(), width, height, bytes(data))
                        self._wgc_condition.notify_all()
                except Exception as error:
                    self.capture_fallback_reason = f"WGC read failed: {error}"
                    self.capture_backend = "GDI"
                    self._wgc = None
                    break
        threading.Thread(target=collect, name="schooldays-original-wgc", daemon=True).start()

    def start(self) -> dict:
        if self.process and self.process.poll() is None:
            return self.status()
        if not ORIGINAL_EXE.is_file():
            raise FileNotFoundError(str(ORIGINAL_EXE))
        self.process = subprocess.Popen([str(ORIGINAL_EXE)], cwd=ORIGINAL_EXE.parent)
        try:
            deadline = time.monotonic() + 25
            while time.monotonic() < deadline:
                self.hwnd = self._find_window(self.process.pid)
                if self.hwnd:
                    self._initialize_wgc()
                    return self.status()
                if self.process.poll() is not None:
                    raise RuntimeError(f"Original exited with {self.process.returncode}")
                time.sleep(0.1)
            raise TimeoutError("Original game window was not found")
        except Exception:
            self.stop()
            raise

    def stop(self) -> dict:
        # Only a process launched by this controller may be terminated.
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.process = None
        self.hwnd = 0
        self._wgc = None
        self._wgc_latest = None
        self.capture_backend = "GDI"
        return {"stopped": True}

    def _find_window(self, pid: int) -> int:
        found = []
        callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def visit(hwnd, _):
            owner = wintypes.DWORD()
            self.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value == pid and self.user32.IsWindowVisible(hwnd):
                found.append(hwnd)
            return True
        self.user32.EnumWindows(callback(visit), 0)
        return found[0] if found else 0

    def _client(self) -> tuple[int, int, int, int]:
        if (not self.hwnd or not self.user32.IsWindow(self.hwnd)) and self.process and self.process.poll() is None:
            self.hwnd = self._find_window(self.process.pid)
        if not self.hwnd or not self.user32.IsWindow(self.hwnd):
            raise RuntimeError("Original window is unavailable")
        rect = wintypes.RECT()
        point = wintypes.POINT(0, 0)
        if not self.user32.GetClientRect(self.hwnd, ctypes.byref(rect)) or not self.user32.ClientToScreen(self.hwnd, ctypes.byref(point)):
            raise OSError("Cannot query original client rectangle")
        return point.x, point.y, rect.right - rect.left, rect.bottom - rect.top

    def status(self) -> dict:
        if (not self.hwnd or not self.user32.IsWindow(self.hwnd)) and self.process and self.process.poll() is None:
            self.hwnd = self._find_window(self.process.pid)
        if not self.hwnd or not self.user32.IsWindow(self.hwnd):
            return {"running": False, "pid": self.process.pid if self.process else None}
        x, y, w, h = self._client()
        return {"pid": self.process.pid if self.process else None,
                "hwnd": int(self.hwnd), "client": {"x": x, "y": y,
                                                    "width": w, "height": h},
                "running": bool(self.hwnd), "captureBackend": self.capture_backend}

    def health(self) -> dict:
        state = self.status()
        os_build = sys.getwindowsversion().build
        return {"serverVersion": SERVER_VERSION,
                "processRunning": self.process is not None and self.process.poll() is None,
                "windowDetected": bool(state.get("hwnd")),
                "captureBackend": self.capture_backend,
                "wgcAvailable": WindowCapture is not None,
                "captureFallbackReason": self.capture_fallback_reason,
                "originalExecutablePresent": ORIGINAL_EXE.is_file(),
                "originalExecutable": str(ORIGINAL_EXE),
                "transportConnected": bool(state.get("running")),
                "bridgeProtocolVersion": None,
                "windowsBuild": os_build,
                "processLoopbackApiEligible": os_build >= 20348,
                "processLoopbackHelperAvailable": False}

    def focus(self) -> None:
        self.user32.ShowWindow(self.hwnd, 9)
        self.user32.SetForegroundWindow(self.hwnd)

    def move(self, x: float, y: float) -> dict:
        if not 0 <= x <= 1 or not 0 <= y <= 1:
            raise ValueError("Coordinates must be normalized to 0..1")
        ox, oy, w, h = self._client()
        self.focus()
        self.user32.SetCursorPos(ox + round(x * w), oy + round(y * h))
        return self.status()

    def click(self, x: float, y: float, action_id: int = 0) -> dict:
        self.move(x, y)
        sent = time.monotonic_ns()
        self.user32.mouse_event(0x0002, 0, 0, 0, 0)
        self.user32.mouse_event(0x0004, 0, 0, 0, 0)
        marker = {"actionId": action_id, "sentMonotonicNs": sent,
                  "kind": "click", "x": x, "y": y, "firstVisualChangeNs": None}
        self.actions.append(marker)
        return {"window": self.status(), "action": marker}

    def key(self, key: str, action_id: int = 0) -> dict:
        names = {"ESCAPE": 0x1B, "ENTER": 0x0D, "SPACE": 0x20,
                 "LEFT": 0x25, "UP": 0x26, "RIGHT": 0x27, "DOWN": 0x28}
        code = names.get(key.upper())
        if code is None and len(key) == 1:
            code = ord(key.upper())
        if code is None:
            raise ValueError("Unsupported original key")
        self.focus()
        sent = time.monotonic_ns()
        self.user32.keybd_event(code, 0, 0, 0)
        self.user32.keybd_event(code, 0, 2, 0)
        marker = {"actionId": action_id, "sentMonotonicNs": sent,
                  "kind": "key", "key": key, "firstVisualChangeNs": None}
        self.actions.append(marker)
        return {"window": self.status(), "action": marker}

    def capture(self, label: str = "original", directory: Path = OUTPUT) -> dict:
        if self._wgc is not None:
            requested = time.monotonic_ns()
            with self._wgc_condition:
                self._wgc_condition.wait_for(
                    lambda: self._wgc_latest is not None and self._wgc_latest[0] >= requested,
                    timeout=0.015)
                frame = self._wgc_latest
            if frame is not None and frame[0] >= requested:
                stamp, width, height, pixels = frame
                image = Image.frombytes("RGBA", (width, height), pixels, "raw", "BGRA")
                self.capture_backend = "WGC"
                self.capture_fallback_reason = ""
                return _png(image, label, directory=directory,
                            captured_ns=stamp, backend="WGC")
            self.capture_fallback_reason = "WGC produced no new frame within 15 ms"
            self.capture_backend = "GDI"
        if self.user32.GetForegroundWindow() != self.hwnd:
            self.focus()
            time.sleep(0.03)
        if self.user32.GetForegroundWindow() != self.hwnd:
            raise RuntimeError("Original window is not foreground; visible GDI capture would be unreliable")
        x, y, width, height = self._client()
        if width <= 0 or height <= 0:
            raise RuntimeError("Original client area is empty")
        screen = self.user32.GetDC(0)
        memory = self.gdi32.CreateCompatibleDC(screen)
        bitmap = self.gdi32.CreateCompatibleBitmap(screen, width, height)
        old = self.gdi32.SelectObject(memory, bitmap)
        selected = True
        try:
            if not self.gdi32.BitBlt(memory, 0, 0, width, height, screen, x, y, 0x00CC0020):
                raise OSError("GDI client capture failed")
            captured_ns = time.monotonic_ns()
            # GetDIBits requires the bitmap to be deselected from its DC.
            self.gdi32.SelectObject(memory, old)
            selected = False
            class Info(ctypes.Structure):
                _fields_ = [("size", wintypes.DWORD), ("width", wintypes.LONG),
                            ("height", wintypes.LONG), ("planes", wintypes.WORD),
                            ("bits", wintypes.WORD), ("compression", wintypes.DWORD),
                            ("image_size", wintypes.DWORD), ("xppm", wintypes.LONG),
                            ("yppm", wintypes.LONG), ("used", wintypes.DWORD),
                            ("important", wintypes.DWORD)]
            info = Info(ctypes.sizeof(Info), width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
            pixels = ctypes.create_string_buffer(width * height * 4)
            if not self.gdi32.GetDIBits(memory, bitmap, 0, height, pixels, ctypes.byref(info), 0):
                raise OSError("GDI pixel read failed")
            image = Image.frombytes("RGB", (width, height), pixels.raw, "raw", "BGRX")
            return _png(image, label, directory=directory,
                        captured_ns=captured_ns, backend="GDI")
        finally:
            if selected:
                self.gdi32.SelectObject(memory, old)
            self.gdi32.DeleteObject(bitmap)
            self.gdi32.DeleteDC(memory)
            self.user32.ReleaseDC(0, screen)
