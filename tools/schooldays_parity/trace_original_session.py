"""Observe a running retail School Days HQ session without modifying its process.

Run with the parity venv's Python and the PID of SCHOOLDAYS HQ.exe. The JSONL
trace contains timing, game-window input, visual hashes, and Save/*.DAT changes.
Optional screenshots remain in the selected local output directory.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import time
from datetime import datetime, timezone

from PIL import ImageGrab


ORIGINAL = Path(r"D:\SCHOOL DAYS HQ\Overflow\SCHOOLDAYS HQ")
USER32 = ctypes.WinDLL("user32", use_last_error=True)
KERNEL32 = ctypes.WinDLL("kernel32", use_last_error=True)
WH_MOUSE_LL = 14
WH_KEYBOARD_LL = 13
WM_LBUTTONDOWN = 0x0201
WM_RBUTTONDOWN = 0x0204
WM_KEYDOWN = 0x0100
WM_SYSKEYDOWN = 0x0104
PM_REMOVE = 0x0001
CONTROL_KEYS = {
    0x08: "BACKSPACE", 0x09: "TAB", 0x0D: "ENTER", 0x1B: "ESCAPE",
    0x20: "SPACE", 0x21: "PAGE_UP", 0x22: "PAGE_DOWN",
    0x23: "END", 0x24: "HOME", 0x25: "LEFT", 0x26: "UP",
    0x27: "RIGHT", 0x28: "DOWN", 0x70: "F1", 0x71: "F2",
    0x72: "F3", 0x73: "F4", 0x74: "F5", 0x75: "F6",
    0x76: "F7", 0x77: "F8", 0x78: "F9", 0x79: "F10",
    0x7A: "F11", 0x7B: "F12",
}


class Point(ctypes.Structure):
    _fields_ = [("x", wintypes.LONG), ("y", wintypes.LONG)]


class MouseHook(ctypes.Structure):
    _fields_ = [("pt", Point), ("mouseData", wintypes.DWORD),
                ("flags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ctypes.c_void_p)]


class KeyboardHook(ctypes.Structure):
    _fields_ = [("vkCode", wintypes.DWORD), ("scanCode", wintypes.DWORD),
                ("flags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ctypes.c_void_p)]


HOOK_CALLBACK = ctypes.WINFUNCTYPE(wintypes.LPARAM, ctypes.c_int,
                                   wintypes.WPARAM, wintypes.LPARAM)
WINDOW_CALLBACK = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND,
                                     wintypes.LPARAM)
USER32.EnumWindows.argtypes = [WINDOW_CALLBACK, wintypes.LPARAM]
USER32.GetForegroundWindow.restype = wintypes.HWND
USER32.IsWindow.argtypes = [wintypes.HWND]
USER32.IsWindowVisible.argtypes = [wintypes.HWND]
USER32.GetWindowThreadProcessId.argtypes = [wintypes.HWND,
                                            ctypes.POINTER(wintypes.DWORD)]
USER32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
USER32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(Point)]
USER32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR,
                                 ctypes.c_int]
USER32.SetWindowsHookExW.argtypes = [ctypes.c_int, HOOK_CALLBACK,
                                    wintypes.HINSTANCE, wintypes.DWORD]
USER32.SetWindowsHookExW.restype = wintypes.HHOOK
USER32.CallNextHookEx.argtypes = [wintypes.HHOOK, ctypes.c_int,
                                 wintypes.WPARAM, wintypes.LPARAM]
USER32.CallNextHookEx.restype = wintypes.LPARAM
USER32.UnhookWindowsHookEx.argtypes = [wintypes.HHOOK]
USER32.PeekMessageW.argtypes = [ctypes.POINTER(wintypes.MSG), wintypes.HWND,
                               wintypes.UINT, wintypes.UINT, wintypes.UINT]
KERNEL32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
KERNEL32.GetModuleHandleW.restype = wintypes.HMODULE


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def game_window(pid: int) -> int:
    matches = []

    def visit(hwnd: int, _unused: int) -> bool:
        owner = wintypes.DWORD()
        USER32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and USER32.IsWindowVisible(hwnd):
            matches.append(hwnd)
        return True

    callback = WINDOW_CALLBACK(visit)
    USER32.EnumWindows(callback, 0)
    return matches[0] if matches else 0


def client_bounds(hwnd: int) -> tuple[int, int, int, int] | None:
    rect = wintypes.RECT()
    origin = Point()
    if not USER32.GetClientRect(hwnd, ctypes.byref(rect)):
        return None
    if not USER32.ClientToScreen(hwnd, ctypes.byref(origin)):
        return None
    width, height = rect.right - rect.left, rect.bottom - rect.top
    if width <= 0 or height <= 0:
        return None
    return origin.x, origin.y, width, height


def window_title(hwnd: int) -> str:
    title = ctypes.create_unicode_buffer(512)
    USER32.GetWindowTextW(hwnd, title, len(title))
    return title.value


def profile_changes(save_dir: Path, previous: dict) -> tuple[list[dict], dict]:
    current = {}
    changes = []
    for path in sorted(save_dir.glob("*.DAT")):
        stat = path.stat()
        old = previous.get(path.name)
        identity = (stat.st_size, stat.st_mtime_ns)
        digest = old[2] if old and old[:2] == identity else sha256_file(path)
        current[path.name] = (*identity, digest)
        if old != current[path.name]:
            changes.append({"name": path.name, "bytes": stat.st_size,
                            "sha256": digest, "change": "modified" if old else "present"})
    for name in previous.keys() - current.keys():
        changes.append({"name": name, "change": "removed"})
    return changes, current


def observe(pid: int, output: Path, duration: float, frame_interval: float,
            screenshots: bool, original: Path) -> Path:
    executable = original / "SCHOOLDAYS HQ.exe"
    if not executable.is_file():
        raise FileNotFoundError(executable)
    hwnd = game_window(pid)
    if not hwnd:
        raise RuntimeError(f"No visible window belongs to PID {pid}")
    output.mkdir(parents=True, exist_ok=True)
    trace = output / "original-session.jsonl"
    pending: list[dict] = []
    last_frame = None
    profile = {}
    mouse_hook = keyboard_hook = 0

    def mouse_callback(code: int, message: int, data: int) -> int:
        if code >= 0 and message in (WM_LBUTTONDOWN, WM_RBUTTONDOWN) and \
                USER32.GetForegroundWindow() == hwnd:
            bounds = client_bounds(hwnd)
            if bounds:
                x, y, width, height = bounds
                point = ctypes.cast(data, ctypes.POINTER(MouseHook)).contents.pt
                if x <= point.x < x + width and y <= point.y < y + height:
                    pending.append({"kind": "mouse", "button": "left" if
                                    message == WM_LBUTTONDOWN else "right",
                                    "x": round((point.x - x) / width, 5),
                                    "y": round((point.y - y) / height, 5),
                                    "monotonic_ns": time.monotonic_ns()})
        return USER32.CallNextHookEx(mouse_hook, code, message, data)

    def keyboard_callback(code: int, message: int, data: int) -> int:
        if code >= 0 and message in (WM_KEYDOWN, WM_SYSKEYDOWN) and \
                USER32.GetForegroundWindow() == hwnd:
            key = ctypes.cast(data, ctypes.POINTER(KeyboardHook)).contents.vkCode
            if key in CONTROL_KEYS:
                pending.append({"kind": "key", "key": CONTROL_KEYS[key],
                                "monotonic_ns": time.monotonic_ns()})
        return USER32.CallNextHookEx(keyboard_hook, code, message, data)

    mouse_callback_ref = HOOK_CALLBACK(mouse_callback)
    keyboard_callback_ref = HOOK_CALLBACK(keyboard_callback)
    module = KERNEL32.GetModuleHandleW(None)
    started = time.monotonic()
    last_scan = last_sample = 0.0
    frame_number = 0
    try:
        mouse_hook = USER32.SetWindowsHookExW(WH_MOUSE_LL, mouse_callback_ref,
                                             module, 0)
        keyboard_hook = USER32.SetWindowsHookExW(WH_KEYBOARD_LL,
                                                keyboard_callback_ref, module, 0)
        if not mouse_hook or not keyboard_hook:
            raise OSError(ctypes.get_last_error(), "Could not install input hooks")
        with trace.open("w", encoding="utf-8", buffering=1) as sink:
            def emit(event_type: str, **fields) -> None:
                sink.write(json.dumps({"type": event_type,
                                       "utc": datetime.now(timezone.utc).isoformat(),
                                       "monotonic_ns": time.monotonic_ns(),
                                       **fields}, ensure_ascii=False) + "\n")

            emit("session", pid=pid, hwnd=hwnd, title=window_title(hwnd),
                 executable=str(executable), executable_sha256=sha256_file(executable),
                 save_dir=str(original / "Save"), method="external_win32_observer",
                 scope="foreground input, visible frames and DAT file changes")
            while USER32.IsWindow(hwnd) and (duration <= 0 or
                                              time.monotonic() - started < duration):
                message = wintypes.MSG()
                while USER32.PeekMessageW(ctypes.byref(message), 0, 0, 0,
                                          PM_REMOVE):
                    USER32.TranslateMessage(ctypes.byref(message))
                    USER32.DispatchMessageW(ctypes.byref(message))
                while pending:
                    action = pending.pop(0)
                    emit("input", **action)
                now = time.monotonic()
                if now - last_scan >= 0.5:
                    changes, profile = profile_changes(original / "Save", profile)
                    for change in changes:
                        emit("save", **change)
                    last_scan = now
                if now - last_sample >= frame_interval and \
                        USER32.GetForegroundWindow() == hwnd:
                    bounds = client_bounds(hwnd)
                    if bounds:
                        x, y, width, height = bounds
                        frame = ImageGrab.grab(bbox=(x, y, x + width, y + height))
                        digest = hashlib.sha256(frame.tobytes()).hexdigest()
                        if digest != last_frame:
                            frame_number += 1
                            image_path = None
                            if screenshots:
                                image_path = f"frame-{frame_number:06d}.png"
                                frame.save(output / image_path)
                            emit("frame", width=width, height=height,
                                 sha256_rgb=digest, image=image_path)
                            last_frame = digest
                    last_sample = now
                time.sleep(0.02)
            emit("end", reason="window_closed" if not USER32.IsWindow(hwnd)
                 else "duration_elapsed")
    finally:
        if mouse_hook:
            USER32.UnhookWindowsHookEx(mouse_hook)
        if keyboard_hook:
            USER32.UnhookWindowsHookEx(keyboard_hook)
    return trace


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True,
                        help="PID of a running SCHOOLDAYS HQ.exe")
    parser.add_argument("--output", type=Path, required=True,
                        help="New or existing local directory for the trace")
    parser.add_argument("--duration", type=float, default=0,
                        help="Seconds to observe; 0 continues until the game closes")
    parser.add_argument("--frame-interval", type=float, default=0.5)
    parser.add_argument("--screenshots", action="store_true",
                        help="Save changed visible frames as local PNGs")
    parser.add_argument("--original", type=Path, default=ORIGINAL)
    args = parser.parse_args()
    if args.frame_interval <= 0 or args.duration < 0:
        parser.error("frame interval must be positive and duration nonnegative")
    try:
        print(observe(args.pid, args.output, args.duration,
                      args.frame_interval, args.screenshots, args.original),
              flush=True)
    except KeyboardInterrupt:
        print("Observation stopped by user", flush=True)


if __name__ == "__main__":
    main()
