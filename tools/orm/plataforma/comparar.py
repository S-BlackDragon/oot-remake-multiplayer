"""Hookshot against our emulator on the same route (docs/PLATAFORMA_HOOKSHOT.md).

    python -I comparar.py hookshot --run <run folder> [--game <folder of the Hookshot build>] [--user-from <folder>]
        starts your Hookshot build without touching its folder: the saves are a copy (--user), the log goes to
        <run>/hookshot_log.txt (stderr), screenshots to <run>/shots; frame generation off, slow frames logged
        (RX_VK_SLOW_LOG=50); samples the process's memory every 5 s to <run>/mem.csv until the game closes.
    python -I comparar.py muestrear --proceso lichii.exe --salida <run>/mem.csv [--cada 5] [--espera 600]
        waits for the process (the kit's emulator) and samples it the same way until it closes.
    python -I comparar.py informe --emu-log <eden_log.txt> [--emu-frames <frames.csv>] [--emu-mem <mem.csv>]
        --hs-log <hookshot_log.txt> [--hs-frames <csv>] [--hs-mem <mem.csv>] [--notas <notas.txt>] [--salida resumen.txt]
        reads both runs and writes the comparison and the decision (move now / wait) in Spanish.

Logs read: the emulator's "FPS: x speed: y%" (every 2 s) and our "coop: frames: ..." lines (LICHII_ORM_FRAME_LOG,
lichii/hook/coop_frame_log.h); Hookshot's "vi: N frames presented (F fps over the last S s" (every 5 s), its "vk: N
presents, F fps" when there is no vi line, and its "vk: slow frame at present N: X ms" (RX_VK_SLOW_LOG). A frame CSV is
"frame,seconds,ms" (ours, or Hookshot's RX_VK_FRAME_LOG in newer builds) or a PresentMon CSV (MsBetweenPresents or
FrameTime). Notes: one line per thing seen, "<H|E> <grave|leve> <text>" (H Hookshot, E emulator). Test:
test_comparar.py (fake logs, no game). Standard library only.
"""
import argparse
import csv
import ctypes
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

SPIKE_MS = 50.0
BIG_SPIKE_MS = 100.0
WARMUP_S = 120.0          # memory: the first two minutes (loading, shaders) are left out of the slope
FPS_GAIN = 1.10           # a gain: Hookshot's median 10 % above ours ...
P5_GAIN = 1.15            # ... or its percentile 5 (the worst moments) 15 % above ours
MEM_SLOPE_MB_10MIN = 50.0  # more growth than this per 10 min is a leak

EMU_FPS = re.compile(r"FPS: ([0-9.]+) speed: ([0-9.]+)%")
EMU_FRAMES = re.compile(r"coop: frames: (\d+) in ([0-9.]+) s \([0-9.]+ fps\), median ([0-9.]+) ms, worst ([0-9.]+) ms, "
                        r"(\d+) of")
HS_VI = re.compile(r"vi: \d+ frames presented \(([0-9.]+) fps over the last ([0-9.]+) s")
HS_VK = re.compile(r"vk: \d+ presents, ([0-9.]+) fps")
HS_SLOW = re.compile(r"vk: slow frame at present \d+: ([0-9.]+) ms")
CRASH = {
    "emu": re.compile(r"unhandled exception|access violation|device lost|VK_ERROR_DEVICE_LOST|Fatal error|crashed",
                      re.I),
    "hs": re.compile(r"CRASH:|exception 0x|access violation|device lost|VK_ERROR_DEVICE_LOST", re.I),
}
NOTE = re.compile(r"^\s*([HE])\s+(grave|leve)\s+(.+)$", re.I)


# ------------------------------------------------------------------------------------------------- reading

def pct(vals: list[float], p: float) -> float | None:
    """The p-th percentile (0-100), nearest rank; None for no values."""
    if not vals:
        return None
    s = sorted(vals)
    k = max(0, min(len(s) - 1, int(round(p / 100.0 * (len(s) - 1)))))
    return s[k]


def read_text(path: str | None) -> str:
    if not path:
        return ""
    return Path(path).read_text(encoding="utf-8", errors="replace")


def log_fps(text: str, kind: str) -> list[tuple[float, float]]:
    """(fps, seconds the value covers) for each fps line of a log."""
    if kind == "emu":
        return [(float(m.group(1)), 2.0) for m in EMU_FPS.finditer(text)]
    vi = [(float(m.group(1)), float(m.group(2))) for m in HS_VI.finditer(text)]
    if vi:
        return vi
    return [(float(m.group(1)), 2.0) for m in HS_VK.finditer(text)]


def crashes(text: str, kind: str) -> list[str]:
    out = []
    for line in text.splitlines():
        if "coop:" in line:   # our own lines can name these words ("a crash before ...")
            continue
        if CRASH[kind].search(line):
            out.append(line.strip()[:200])
    return out


def frame_times(path: str | None) -> list[float]:
    """Frame intervals in ms from a frame CSV (ours / RX_VK_FRAME_LOG, or PresentMon's); the first frame's 0 is left out."""
    if not path:
        return []
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        rows = list(csv.reader(f))
    if not rows:
        return []
    head = [h.strip() for h in rows[0]]
    for name in ("MsBetweenPresents", "FrameTime", "ms"):
        if name in head:
            col, data = head.index(name), rows[1:]
            break
    else:
        col, data = 2, rows  # "frame,seconds,ms" without its header line
    out = []
    for r in data:
        try:
            v = float(r[col])
        except (IndexError, ValueError):
            continue
        if v > 0:
            out.append(v)
    return out


def memory(path: str | None) -> list[tuple[float, float]]:
    """(seconds, private MB) from a mem.csv of `muestrear`."""
    if not path:
        return []
    with open(path, newline="", encoding="utf-8") as f:
        return [(float(r["t_s"]), float(r["priv_mb"])) for r in csv.DictReader(f) if r.get("priv_mb")]


def slope_per_10min(pts: list[tuple[float, float]]) -> float | None:
    """Theil-Sen slope of (seconds, MB) after the warm-up, in MB per 10 minutes; None under 3 points."""
    pts = [p for p in pts if p[0] >= WARMUP_S]
    if len(pts) < 3:
        return None
    slopes = [(b[1] - a[1]) / (b[0] - a[0]) for i, a in enumerate(pts) for b in pts[i + 1:] if b[0] != a[0]]
    return statistics.median(slopes) * 600.0 if slopes else None


def notes(text: str) -> dict:
    out = {"H": {"grave": [], "leve": []}, "E": {"grave": [], "leve": []}}
    for line in text.splitlines():
        m = NOTE.match(line)
        if m:
            out[m.group(1).upper()][m.group(2).lower()].append(m.group(3).strip())
    return out


def run_metrics(kind: str, log_text: str, frames_csv: str | None, mem_csv: str | None, slow_log: bool = True) -> dict:
    fps = log_fps(log_text, kind)
    vals = [v for v, _ in fps]
    seconds = sum(s for _, s in fps)
    ft = frame_times(frames_csv)
    m: dict = {"fps_n": len(vals), "fps_med": statistics.median(vals) if vals else None,
               "fps_p5": pct(vals, 5), "fps_min": min(vals) if vals else None, "minutes": seconds / 60.0,
               "crashes": crashes(log_text, kind), "spikes_from": None}
    if ft:
        minutes = sum(ft) / 60000.0
        m.update(spikes_min=sum(1 for v in ft if v >= SPIKE_MS) / minutes if minutes else None,
                 big_min=sum(1 for v in ft if v >= BIG_SPIKE_MS) / minutes if minutes else None,
                 ft_p99=pct(ft, 99), ft_worst=max(ft), spikes_from="CSV de fotogramas")
    elif kind == "emu" and EMU_FRAMES.search(log_text):
        wins = [(int(g[0]), float(g[1]), float(g[3]), int(g[4])) for g in EMU_FRAMES.findall(log_text)]
        minutes = sum(w[1] for w in wins) / 60.0
        m.update(spikes_min=sum(w[3] for w in wins) / minutes if minutes else None, big_min=None, ft_p99=None,
                 ft_worst=max(w[2] for w in wins), spikes_from="líneas coop: frames")
    elif kind == "hs" and seconds and slow_log:
        # RX_VK_SLOW_LOG=50 (set by `comparar.py hookshot`): frames of 50 to 500 ms, 800 lines at most
        slow = [float(x) for x in HS_SLOW.findall(log_text)]
        m.update(spikes_min=sum(1 for v in slow if v >= SPIKE_MS) / (seconds / 60.0),
                 big_min=sum(1 for v in slow if v >= BIG_SPIKE_MS) / (seconds / 60.0), ft_p99=None,
                 ft_worst=max(slow) if slow else None, spikes_from="RX_VK_SLOW_LOG")
    mem = memory(mem_csv)
    m["mem_peak"] = max(v for _, v in mem) if mem else None
    m["mem_slope"] = slope_per_10min(mem)
    return m


# ------------------------------------------------------------------------------------------------- deciding

def decide(emu: dict, hs: dict, seen: dict) -> tuple[str, list[str]]:
    """The verdict and one line per rule (docs/PLATAFORMA_HOOKSHOT.md, "La regla")."""
    lines, missing, fails = [], [], []

    def rule(name: str, ok: bool | None, text: str):
        mark = "SIN DATOS" if ok is None else "cumple" if ok else "NO cumple"
        lines.append(f"{name}: {mark}: {text}")
        if ok is None:
            missing.append(name)
        elif not ok:
            fails.append(name)

    rule("R1 estabilidad", not hs["crashes"],
         f"Hookshot {len(hs['crashes'])} cierres o GPU perdida, emulador {len(emu['crashes'])}")
    if emu["fps_med"] is None or hs["fps_med"] is None:
        rule("R2 fps", None, "falta un registro con líneas de fps")
    else:
        # Both are capped at 60: a clear gain in the median or in the worst moments (percentile 5), and no loss in
        # the median.
        not_worse = hs["fps_med"] >= emu["fps_med"] - 1.0
        gain = hs["fps_med"] >= emu["fps_med"] * FPS_GAIN or hs["fps_p5"] >= emu["fps_p5"] * P5_GAIN
        rule("R2 fps", not_worse and gain,
             f"mediana Hookshot {hs['fps_med']:.1f} frente a {emu['fps_med']:.1f} (ganancia: {emu['fps_med'] * FPS_GAIN:.1f}); "
             f"percentil 5 {hs['fps_p5']:.1f} frente a {emu['fps_p5']:.1f} (ganancia: {emu['fps_p5'] * P5_GAIN:.1f})")
    if emu.get("spikes_min") is None or hs.get("spikes_min") is None:
        rule("R3 tirones", None, "falta el registro de fotogramas de uno de los dos (LICHII_ORM_FRAME_LOG / "
                                 "RX_VK_SLOW_LOG=50 o PresentMon)")
    else:
        rule("R3 tirones", hs["spikes_min"] <= emu["spikes_min"],
             f"fotogramas de {SPIKE_MS:.0f} ms o más por minuto: Hookshot {hs['spikes_min']:.2f}, emulador "
             f"{emu['spikes_min']:.2f}")
    rule("R4 visual", not seen["H"]["grave"],
         f"{len(seen['H']['grave'])} fallos graves vistos en Hookshot, {len(seen['E']['grave'])} en el emulador")
    if hs["mem_slope"] is None:
        rule("R5 memoria", None, "falta mem.csv de Hookshot (o la partida duró menos de 3 muestras tras 2 min)")
    else:
        rule("R5 memoria", hs["mem_slope"] <= MEM_SLOPE_MB_10MIN,
             f"Hookshot crece {hs['mem_slope']:+.0f} MB cada 10 min (límite {MEM_SLOPE_MB_10MIN:.0f}); emulador "
             + (f"{emu['mem_slope']:+.0f}" if emu["mem_slope"] is not None else "sin dato"))
    if fails:
        verdict = "ESPERAR: no cumple " + ", ".join(fails)
    elif missing:
        verdict = "ESPERAR (faltan datos): " + ", ".join(missing)
    else:
        verdict = "MOVER YA: empezar el adaptador de Hookshot (docs/PLATAFORMA_HOOKSHOT.md, fase H1)"
    return verdict, lines


def fmt(v, d: int = 1) -> str:
    return "-" if v is None else f"{v:.{d}f}"


def report(a) -> str:
    emu = run_metrics("emu", read_text(a.emu_log), a.emu_frames, a.emu_mem)
    hs = run_metrics("hs", read_text(a.hs_log), a.hs_frames, a.hs_mem, slow_log=not a.hs_sin_slow_log)
    seen = notes(read_text(a.notas))
    verdict, lines = decide(emu, hs, seen)
    out = [f"DECISIÓN: {verdict}", ""]
    out += lines
    out += ["", f"{'':24}{'emulador':>12}{'Hookshot':>12}"]
    for label, key, d in (("minutos con fps", "minutes", 1), ("fps mediana", "fps_med", 1), ("fps percentil 5", "fps_p5", 1),
                          ("fps mínimo", "fps_min", 1), ("tirones >=50 ms / min", "spikes_min", 2),
                          ("tirones >=100 ms / min", "big_min", 2), ("fotograma p99 (ms)", "ft_p99", 1),
                          ("peor fotograma (ms)", "ft_worst", 1), ("memoria pico (MB)", "mem_peak", 0),
                          ("memoria MB / 10 min", "mem_slope", 0)):
        out.append(f"{label:24}{fmt(emu.get(key), d):>12}{fmt(hs.get(key), d):>12}")
    out.append(f"{'tirones medidos con':24}{emu.get('spikes_from') or '-':>12}  {hs.get('spikes_from') or '-'}")
    for name, m in (("emulador", emu), ("Hookshot", hs)):
        for c in m["crashes"][:5]:
            out.append(f"cierre ({name}): {c}")
    for who, name in (("E", "emulador"), ("H", "Hookshot")):
        for level in ("grave", "leve"):
            for t in seen[who][level]:
                out.append(f"visto ({name}, {level}): {t}")
    return "\n".join(out) + "\n"


# ------------------------------------------------------------------------------------------------- running

def _pids(image: str) -> list[int]:
    """PIDs of the processes with that image name (tasklist, no window)."""
    r = subprocess.run(["tasklist", "/FO", "CSV", "/NH", "/FI", f"IMAGENAME eq {image}"], capture_output=True,
                       text=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    out = []
    for row in csv.reader(r.stdout.splitlines()):
        if len(row) > 1 and row[0].lower() == image.lower():
            try:
                out.append(int(row[1]))
            except ValueError:
                pass
    return out


class _PMC(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [(n, ctypes.c_size_t) for n in (
        "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage",
        "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


def proc_metrics(pid: int) -> dict | None:
    """Working set and private bytes (MB) and handles of a process of this user; None once it is gone (as
    tools/harness/aguante.py reads them: no window, no elevation)."""
    if os.name != "nt":
        return None
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.OpenProcess.restype = wintypes.HANDLE
    k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    k32.CloseHandle.argtypes = [wintypes.HANDLE]
    k32.K32GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
    k32.GetProcessHandleCount.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    k32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    h = k32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return None
    try:
        code = wintypes.DWORD()
        if k32.GetExitCodeProcess(h, ctypes.byref(code)) and code.value != 259:  # not STILL_ACTIVE
            return None
        pmc = _PMC()
        pmc.cb = ctypes.sizeof(_PMC)
        if not k32.K32GetProcessMemoryInfo(h, ctypes.byref(pmc), pmc.cb):
            return None
        n = wintypes.DWORD()
        handles = n.value if k32.GetProcessHandleCount(h, ctypes.byref(n)) else None
        return {"ws_mb": pmc.WorkingSetSize / 2**20, "priv_mb": pmc.PrivateUsage / 2**20, "handles": handles}
    finally:
        k32.CloseHandle(h)


def sample(pid: int, out: Path, every: float, proc=None) -> int:
    """Memory of `pid` every `every` s to `out` (t_s, ws_mb, priv_mb, handles) until it ends; the number of rows."""
    t0 = time.monotonic()
    n = 0
    with open(out, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["t_s", "ws_mb", "priv_mb", "handles"])
        while True:
            if proc is not None and proc.poll() is not None:
                break
            m = proc_metrics(pid)
            if m is None:
                break
            w.writerow([f"{time.monotonic() - t0:.1f}", f"{m['ws_mb']:.1f}", f"{m['priv_mb']:.1f}", m["handles"]])
            f.flush()
            n += 1
            time.sleep(every)
    return n


def run_hookshot(a) -> int:
    game, run = Path(a.game), Path(a.run)
    exe = game / "hookshot.exe"
    if not exe.exists() or not (game / "image.bin").exists():
        print(f"no Hookshot build at {game}")
        return 2
    if _pids("hookshot.exe"):
        print("hookshot.exe is already running: close it first")
        return 2
    run.mkdir(parents=True, exist_ok=True)
    user = run / "user"
    src = Path(a.user_from) if a.user_from else game / "user"
    if not user.exists() and src.exists():
        shutil.copytree(src, user)  # a copy: the build's own saves are never written
    (run / "shots").mkdir(exist_ok=True)
    env = dict(os.environ)
    env.update({"RX_VK_SLOW_LOG": "50", "HOS_FPS_LOG": "1", "RX_VK_FRAMEGEN": "0", "HOOKSHOT_NO_UPDATE_CHECK": "1",
                "RX_VK_SHOTS": str(run / "shots"), "RX_VK_SHOT_EVERY": str(a.shot_every)})
    log = open(run / "hookshot_log.txt", "w", encoding="utf-8")  # stderr taken: the log is not written beside the exe
    proc = subprocess.Popen([str(exe), str(game / "image.bin"), "--romfs", str(game / "romfs"), "--user", str(user)],
                            cwd=str(game), env=env, stdout=subprocess.DEVNULL, stderr=log)
    print(f"Hookshot started (pid {proc.pid}); play the route, then close the game. Log: {run / 'hookshot_log.txt'}")
    rows = sample(proc.pid, run / "mem.csv", a.cada, proc)
    proc.wait()
    log.close()
    print(f"Hookshot closed (exit code {proc.returncode}); {rows} memory samples in {run / 'mem.csv'}")
    (run / "salida.txt").write_text(f"exit code {proc.returncode}\n", encoding="utf-8")
    return 0


def run_sample(a) -> int:
    t_end = time.monotonic() + a.espera
    while time.monotonic() < t_end:
        pids = _pids(a.proceso)
        if pids:
            print(f"{a.proceso} found (pid {pids[0]}): sampling every {a.cada} s until it closes")
            Path(a.salida).parent.mkdir(parents=True, exist_ok=True)
            rows = sample(pids[0], Path(a.salida), a.cada)
            print(f"{rows} samples in {a.salida}")
            return 0
        time.sleep(2)
    print(f"{a.proceso} did not start within {a.espera} s")
    return 1


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    h = sub.add_parser("hookshot")
    h.add_argument("--run", required=True)
    h.add_argument("--game", default=os.environ.get("ORM_HOOKSHOT_GAME", ""), help="folder of the Hookshot build (default: env ORM_HOOKSHOT_GAME)")
    h.add_argument("--user-from", default=None)
    h.add_argument("--cada", type=float, default=5.0)
    h.add_argument("--shot-every", type=int, default=1800)
    s = sub.add_parser("muestrear")
    s.add_argument("--proceso", required=True)
    s.add_argument("--salida", required=True)
    s.add_argument("--cada", type=float, default=5.0)
    s.add_argument("--espera", type=float, default=600.0)
    r = sub.add_parser("informe")
    for k in ("emu-log", "hs-log"):
        r.add_argument(f"--{k}", required=True)
    for k in ("emu-frames", "emu-mem", "hs-frames", "hs-mem", "notas", "salida"):
        r.add_argument(f"--{k}", default=None)
    r.add_argument("--hs-sin-slow-log", action="store_true",
                   help="the Hookshot run had no RX_VK_SLOW_LOG=50 (started by hand): its spikes are not known")
    a = p.parse_args(argv)
    if a.cmd == "hookshot":
        return run_hookshot(a)
    if a.cmd == "muestrear":
        return run_sample(a)
    text = report(a)
    if a.salida:
        Path(a.salida).write_text(text, encoding="utf-8")
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
