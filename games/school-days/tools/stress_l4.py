#!/usr/bin/env python3
"""School Days HQ L4 repeatable *isolated* Windows regression stress.

Only uses synthetic game fixtures and new directories beneath build/. Never
runs the original retail executable, writes real saves, or edits GPKs. Use
--gpu to include real Windows Direct3D12/OpenGL renderer creation; the default
is safe headless/software. This is not a claim of exhaustive route/gameplay QA.
"""

import argparse
import datetime as dt
import hashlib
import itertools
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import time


REPO = Path(__file__).resolve().parents[3]
DEFAULT_BUILD = REPO / "build" / "school-days-l4-two-dll-staticmd"
ROUTE = REPO / "games" / "school-days" / "game-assets" / "school-days-hq.ktnroute"


def running_game():
    if os.name != "nt":
        return False
    try:
        result = subprocess.run(
            ["tasklist", "/FI", "IMAGENAME eq SchoolDays.exe", "/FO", "CSV", "/NH"],
            capture_output=True, text=True, timeout=6, check=False,
        )
        return '"SchoolDays.exe"' in result.stdout
    except (OSError, subprocess.TimeoutExpired):
        raise RuntimeError("Cannot verify existing SchoolDays.exe processes; aborting")


def snapshot_gpks(root):
    if not root.is_dir():
        return {}
    result = {}
    for file in root.iterdir():
        if file.is_file() and file.suffix.lower() == ".gpk":
            stat = file.stat()
            result[file.name] = [stat.st_size, stat.st_mtime_ns]
    return result


def run_command(command, env, timeout, cwd):
    start = time.monotonic()
    try:
        kwargs = {"cwd": str(cwd), "env": env, "capture_output": True,
                  "text": True, "errors": "replace", "timeout": timeout,
                  "check": False}
        if os.name == "nt":
            kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW
        result = subprocess.run(command, **kwargs)
        return {"exit": result.returncode,
                "duration_seconds": round(time.monotonic() - start, 3),
                "stdout_tail": result.stdout[-6000:],
                "stderr_tail": result.stderr[-4000:]}
    except subprocess.TimeoutExpired as error:
        return {"exit": None, "duration_seconds": round(time.monotonic() - start, 3),
                "error": f"timeout after {timeout}s: {error}"}


def synthetic_cases(count, seed, gpu):
    drivers = ["software"] + (["direct3d12", "opengl"] if gpu else [])
    # 0 None, 1 Linear, 2 MPV/OpenGL, 4 Auto. Include *deliberately*
    # incompatible D3D12+MPV to ensure the engine logs a visible fallback.
    backends = {"software": [0, 1, 2, 4],
                "direct3d12": [0, 1, 2, 4],
                "opengl": [0, 1, 2, 4]}
    factors = [1.0, 1.5, 2.0, 4.0]
    presets = [0, 1, 3, 14]  # Anime4K, ArtCNN C4F16/C4F32, fast
    render_scales = [0.75, 1.0]
    all_cases = list(itertools.product(drivers, factors, render_scales))
    rng = random.Random(seed)
    rng.shuffle(all_cases)
    # Ensure every requested driver and backend occurs at least once.
    covered = [(driver, backend, 2.0, 1.0, presets[i % len(presets)])
               for driver in drivers for i, backend in enumerate(backends[driver])]
    for i, (driver, factor, render_scale) in enumerate(all_cases):
        covered.append((driver, backends[driver][i % len(backends[driver])],
                        factor, render_scale, presets[i % len(presets)]))
    while len(covered) < count:
        driver = rng.choice(drivers)
        covered.append((driver, rng.choice(backends[driver]),
                        rng.choice(factors), rng.choice(render_scales),
                        rng.choice(presets)))
    return covered[:max(count, sum(len(backends[x]) for x in drivers))]


def smoke(exe, fixture, route, output, index, values, timeout, env):
    renderer, backend, factor, scale, preset = values
    slot = output / f"case-{index:03d}"
    docs = slot / "documents"
    docs.mkdir(parents=True, exist_ok=False)
    settings = slot / "settings.json"
    log = slot / "engine.log"
    settings.write_text(json.dumps({
        "version": 1, "renderer": renderer, "width": 960, "height": 540,
        "mode": 0, "resizable": False, "vsync": False,
        "upscaler": backend, "preset": preset, "factor": factor,
        "renderScale": scale, "shaderFiles": "",
    }), encoding="utf-8")
    case_env = env.copy()
    case_env["SDL_VIDEODRIVER"] = "dummy" if renderer == "software" else "windows"
    case_env["SDL_AUDIODRIVER"] = "dummy"
    args = [str(exe), "--school-days", str(fixture),
            "--ktrf", str(route), "--documents", str(docs),
            "--config", str(settings), "--no-launcher", "--no-asset-overlay",
            "--renderer", renderer, "--node", "0", "--frames", "6",
            "--log", str(log)]
    result = run_command(args, case_env, timeout, exe.parent)
    log_text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    result["case"] = {"renderer": renderer, "backend": backend,
                      "factor": factor, "renderScale": scale, "preset": preset}
    result["engine_lines"] = [x.strip() for x in log_text.splitlines()
                              if "Engine started" in x or "Upscaler" in x or
                              "unavailable" in x or "shader" in x.lower()][-15:]
    ok = result["exit"] == 0 and f"Engine started ({renderer} renderer)" in log_text
    if backend == 4 and factor > 1 and renderer in ("direct3d12", "opengl"):
        expected = "Auto -> Linear" if renderer == "direct3d12" else "Auto -> MPV"
        ok = ok and expected in log_text
    if backend == 2 and renderer != "opengl" and factor > 1:
        ok = ok and "unavailable" in log_text and "using Auto" in log_text
    if backend == 2 and renderer == "opengl" and factor > 1:
        ok = ok and "Upscaler backend=2" in log_text
    result["pass"] = bool(ok)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--cycles", type=int, default=2,
                        help="Full CTest passes (0 to skip)")
    parser.add_argument("--smokes", type=int, default=36,
                        help="Renderer/settings process restarts")
    parser.add_argument("--gpu", action="store_true",
                        help="Also exercise Windows Direct3D12 and OpenGL")
    parser.add_argument("--timeout", type=int, default=25)
    parser.add_argument("--seed", type=int, default=20261109)
    args = parser.parse_args()
    if not (0 <= args.cycles <= 20 and 0 <= args.smokes <= 1000 and
            2 <= args.timeout <= 180):
        parser.error("cycles 0..20, smokes 0..1000, timeout 2..180")
    build = args.build_dir.resolve()
    exe = build / "games" / "school-days" / "Release" / "SchoolDays.exe"
    fixture = build / "games" / "school-days" / "tests" / "fixtures"
    if not (exe.is_file() and ROUTE.is_file() and
            (fixture / "Script").is_dir()):
        parser.error("L4 build/fixtures are absent; compile and run CTest first")
    if running_game():
        parser.error("A SchoolDays.exe process already exists; close it before stress")
    timestamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    output = build / "qa-stress" / f"stress-{timestamp}"
    output.mkdir(parents=True, exist_ok=False)
    if not output.resolve().is_relative_to(build):
        raise RuntimeError("Refusing to write outside the isolated build tree")
    published_gpks = REPO / "build" / "school-days-dev-control4" / "games" / "school-days" / "Release" / "Packs"
    before = snapshot_gpks(published_gpks)
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    report = {"schema": 1, "created": timestamp, "build": str(build),
              "exe_sha256": digest, "seed": args.seed, "gpu": args.gpu,
              "cases": [], "ctest": [], "gpk_metadata_before": before}
    env = os.environ.copy()
    env["PATH"] = str(exe.parent) + os.pathsep + env.get("PATH", "")
    env["SDL_VIDEODRIVER"] = "dummy"
    env["SDL_AUDIODRIVER"] = "dummy"
    failed = False
    try:
        for cycle in range(args.cycles):
            print(f"[CTest {cycle + 1}/{args.cycles}] 70 tests", flush=True)
            result = run_command(["ctest", "--test-dir", str(build), "-C", "Release",
                                  "--output-on-failure", "--parallel", "2", "--timeout", "120"],
                                 env, 600, REPO)
            result["pass"] = result["exit"] == 0 and "100% tests passed" in result.get("stdout_tail", "")
            report["ctest"].append(result)
            print(f"  {'PASS' if result['pass'] else 'FAIL'} ({result['duration_seconds']}s)", flush=True)
            if not result["pass"]:
                failed = True
                break
        if not failed:
            cases = synthetic_cases(args.smokes, args.seed, args.gpu) if args.smokes else []
            for index, values in enumerate(cases, 1):
                result = smoke(exe, fixture, ROUTE, output, index, values,
                               args.timeout, env)
                report["cases"].append(result)
                print(f"[Smoke {index}/{len(cases)}] {values[0]}/up={values[1]} "
                      f"factor={values[2]} preset={values[4]}: "
                      f"{'PASS' if result['pass'] else 'FAIL'}", flush=True)
                if not result["pass"]:
                    failed = True
    finally:
        after = snapshot_gpks(published_gpks)
        report["gpk_metadata_after"] = after
        report["gpk_unchanged"] = before == after
        report["pass"] = not failed and report["gpk_unchanged"]
        (output / "stress-report.json").write_text(
            json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"REPORT: {output / 'stress-report.json'}", flush=True)
        print(f"RESULT: {'PASS' if report['pass'] else 'FAIL'}; "
              f"CTest={len(report['ctest'])}; Smokes={len(report['cases'])}; "
              f"GPK_UNCHANGED={report['gpk_unchanged']}", flush=True)
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
