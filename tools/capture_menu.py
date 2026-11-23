#!/usr/bin/env python3
"""Capture a Kotonoha game port's main menu (title screen) on macOS.

The helper launches a built .app with the given CLI, waits for its window,
optionally supplies input to skip a splash, captures just the window, crops
the macOS title bar, downsizes to a sensible size and writes menu.png plus
menu.md evidence.

Example:
  python3 tools/capture_menu.py --game DDLC --app DokiDoki \
      --asset "/Volumes/Nero 2TB/KotonohaAssets/DDLC" \
      --behavior "Original DDLC main menu: New Game / Load Game / Settings / Help / Quit" \
      --wait 6 -- --ddlc "/Volumes/Nero 2TB/KotonohaAssets/DDLC" \
      --no-config --frames 100000
"""

import argparse
import datetime as dt
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

OUTPUTS = Path("/Users/nero/Codes/Kotonoha/out/build/macos-arm64-debug/outputs")
EVIDENCE = Path("/Users/nero/Codes/Kotonoha/evidence/menus")
TEMP = Path("/private/var/folders/m4/75wbzb6924vfj11bz9_43f400000gn/T/opencode")

KEYS = {
    "return": 36, "enter": 36, "space": 49, "escape": 53, "esc": 53,
    "up": 126, "down": 125, "left": 123, "right": 124,
    "z": 6, "x": 7, "a": 0, "s": 1, "tab": 48, "delete": 51,
}

APPLESCRIPT_TIMEOUT = 'with timeout of 10 seconds\n%s\nend timeout'


def run(cmd, check=False, capture=True):
    return subprocess.run(cmd, check=check, text=True,
                          capture_output=capture)


def applescript(script):
    result = run(["osascript", "-e", APPLESCRIPT_TIMEOUT % script],
                 capture=True)
    if result.returncode != 0:
        return ""
    return result.stdout.strip()


def pids_for(process):
    result = run(["pgrep", "-x", process], capture=True)
    try:
        return {int(line) for line in result.stdout.split()}
    except ValueError:
        return set()


def wait_for_new_process(process, before, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        after = pids_for(process)
        new = sorted(after - before)
        if new:
            return new[0]
        time.sleep(0.4)
    return None


def window_bounds(process):
    script = ('tell application "System Events" to tell process "%s" to '
              'get {position, size} of every window' % process)
    output = applescript(script)
    if not output:
        return None
    numbers = [int(part) for part in re.findall(r"-?\d+", output)]
    windows = []
    for i in range(0, len(numbers) - 3, 4):
        x, y, w, h = numbers[i:i + 4]
        if w >= 320 and h >= 240:
            windows.append((w * h, x, y, w, h))
    if not windows:
        return None
    _, x, y, w, h = max(windows)
    return x, y, w, h


def wait_for_window(process, timeout):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = window_bounds(process)
        if last:
            return last
        time.sleep(0.5)
    return last


def send_key(name):
    code = KEYS.get(name.lower())
    if code is None:
        raise ValueError("Unknown key %r (known: %s)" %
                         (name, ", ".join(sorted(KEYS))))
    applescript('tell application "System Events" to key code %d' % code)


def send_click(x, y):
    applescript('tell application "System Events" to click at {%d, %d}' %
                (int(x), int(y)))


def parse_steps(steps, default_wait):
    actions = [("sleep", default_wait)]
    for item in steps.split(","):
        item = item.strip()
        if not item:
            continue
        kind, _, value = item.partition(":")
        if kind == "sleep":
            actions.append(("sleep", float(value)))
        elif kind == "key":
            actions.append(("key", value))
        elif kind == "click":
            x, y = value.split(",")
            actions.append(("click", (int(x), int(y))))
        else:
            raise ValueError("Unknown step %r" % item)
    return actions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", required=True, help="evidence folder name")
    parser.add_argument("--app", required=True, help="app name or .app path")
    parser.add_argument("--asset", required=True, help="asset path used")
    parser.add_argument("--proc", default=None, help="process name (default: app name)")
    parser.add_argument("--out", default=None, help="output directory")
    parser.add_argument("--wait", type=float, default=6.0,
                        help="seconds to wait after window appears")
    parser.add_argument("--timeout", type=float, default=90.0,
                        help="window startup timeout")
    parser.add_argument("--steps", default="",
                        help="comma-separated sleep:N,key:NAME,click:X,Y")
    parser.add_argument("--titlebar", type=int, default=32,
                        help="title bar height in points to crop (0 = none)")
    parser.add_argument("--behavior", default="", help="menu behavior statement")
    # Split argv at the first literal "--" ourselves: argparse's REMAINDER
    # cannot reliably consume game flags that themselves start with "--".
    if "--" in sys.argv:
        split = sys.argv.index("--")
        game_argv, sys.argv = sys.argv[split + 1:], sys.argv[:split]
    else:
        game_argv = []
    args = parser.parse_args()

    app_arg = Path(args.app)
    app_path = app_arg if app_arg.is_absolute() and app_arg.exists() else \
        OUTPUTS / (args.app if args.app.endswith(".app") else args.app + ".app")
    if not app_path.exists():
        print("ERROR: app not found: %s" % app_path, file=sys.stderr)
        return 2
    process = args.proc or app_path.stem
    out_dir = Path(args.out) if args.out else EVIDENCE / args.game
    out_dir.mkdir(parents=True, exist_ok=True)
    cli = list(game_argv)

    before = pids_for(process)
    launch = ["open", "-n", "-a", str(app_path)]
    if cli:
        launch += ["--args"] + cli
    launched = run(launch, capture=True)
    if launched.returncode != 0:
        print("ERROR: launch failed: %s" % launched.stderr.strip(),
              file=sys.stderr)
        return 2
    pid = wait_for_new_process(process, before, 30)
    if not pid:
        print("ERROR: process %s did not start" % process, file=sys.stderr)
        return 2
    bounds = wait_for_window(process, args.timeout)
    if not bounds:
        print("ERROR: no window for %s (pid %d)" % (process, pid),
              file=sys.stderr)
        run(["kill", "-TERM", str(pid)])
        return 2
    applescript('tell application "System Events" to set frontmost of '
                'process "%s" to true' % process)
    time.sleep(0.4)

    for kind, value in parse_steps(args.steps, args.wait):
        if kind == "sleep":
            time.sleep(value)
        elif kind == "key":
            send_key(value)
            time.sleep(0.4)
        elif kind == "click":
            send_click(*value)
            time.sleep(0.4)

    bounds = window_bounds(process) or bounds
    applescript('tell application "System Events" to set frontmost of '
                'process "%s" to true' % process)
    time.sleep(0.3)
    x, y, w, h = bounds
    TEMP.mkdir(parents=True, exist_ok=True)
    raw = TEMP / ("menu_%s_raw.png" % args.game)
    if raw.exists():
        raw.unlink()
    shot = run(["screencapture", "-x", "-R", "%d,%d,%d,%d" % (x, y, w, h),
                str(raw)], capture=True)
    if shot.returncode != 0 or not raw.exists():
        print("ERROR: screencapture failed: %s" % shot.stderr.strip(),
              file=sys.stderr)
        run(["kill", "-TERM", str(pid)])
        return 2

    from PIL import Image, ImageStat
    image = Image.open(raw).convert("RGB")
    original = image.size
    scale = original[1] / h if h else 2
    crop = int(round(args.titlebar * scale))
    if crop:
        image = image.crop((0, crop, image.width, image.height))
    image.thumbnail((1280, 800), Image.LANCZOS)
    png = out_dir / "menu.png"
    image.save(png)
    stat = ImageStat.Stat(image)
    deviation = sum(stat.stddev) / 3
    status = "PASS" if deviation > 8 else "FAIL (frame looks blank/uniform)"

    now = dt.datetime.now().astimezone().isoformat(timespec="seconds")
    command = " ".join(launch)
    md = out_dir / "menu.md"
    md.write_text(
        "# Main menu evidence: %s\n\n"
        "- Status: %s (visual verification pending)\n"
        "- Timestamp: %s\n"
        "- App: `%s` (process `%s`, pid %d)\n"
        "- CLI: `%s`\n"
        "- Asset path: `%s`\n"
        "- Window bounds (points): `%d,%d,%d,%d`\n"
        "- Raw capture: `%s` (%dx%d) -> `%s` (%dx%d)\n"
        "- Mean pixel standard deviation: %.1f\n"
        "- Original menu behavior targeted: %s\n"
        % (args.game, status, now, app_path, process, pid, command,
           args.asset, x, y, w, h, raw.name, original[0], original[1],
           png.name, image.width, image.height, deviation,
           args.behavior or "title screen / main menu"),
        encoding="utf-8")

    run(["kill", "-TERM", str(pid)])
    time.sleep(0.8)
    if pid in pids_for(process):
        run(["kill", "-KILL", str(pid)])
    print("%s: %s -> %s" % (args.game, status, png))
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
