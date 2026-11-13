"""Tests for comparar.py with fake logs (no game): python -I tools/orm/plataforma/test_comparar.py"""
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import comparar as c  # noqa: E402

EMU_LOG = "\n".join(
    ["[  1.0] Core <Info> core.cpp:1:Log: coop: loaded (ENet 1.3.18)"]
    + [f"[ {10 + 2 * i}.0] Render <Info> perf.cpp:9:Log: FPS: {v} speed: 100.0%" for i, v in
       enumerate([58.0, 59.5, 60.0, 44.0, 60.0, 59.0, 57.0, 60.0, 60.0, 52.0])]
    + ["[ 40.0] Core <Info> coop_frame_log.cpp:1:Tick: coop: frames: 590 in 10.0 s (59.0 fps), median 16.7 ms, "
       "worst 81.0 ms, 3 of 50 ms or more",
       "[ 50.0] Core <Info> coop_hook.cpp:1:Log: coop: a crash before the title is not ours (test line)"])

HS_LOG = "\n".join(
    ["[hos main] Hookshot 2.0.1"]
    + [f"[hos Presentation Thread] vi: {300 * (i + 1)} frames presented ({v} fps over the last 5.0 s, interval 1)"
       for i, v in enumerate([62.0, 66.0, 70.0, 68.0, 71.0, 69.0])]
    + ["[hos Presentation Thread] vk: slow frame at present 400: 72.5 ms; submit 1.0, draw 2.0",
       "[hos Presentation Thread] vk: slow frame at present 900: 120.0 ms; submit 1.0, draw 2.0"])


def write(tmp: Path, name: str, text: str) -> str:
    p = tmp / name
    p.write_text(text, encoding="utf-8")
    return str(p)


class Reading(unittest.TestCase):
    def test_fps_lines(self):
        emu = c.log_fps(EMU_LOG, "emu")
        self.assertEqual(len(emu), 10)
        self.assertEqual(emu[3], (44.0, 2.0))
        hs = c.log_fps(HS_LOG, "hs")
        self.assertEqual(hs[0], (62.0, 5.0))
        # no vi line: the vk ones
        self.assertEqual(c.log_fps("vk: 120 presents, 40.8 fps; a frame: 2 draws", "hs"), [(40.8, 2.0)])

    def test_crashes_skip_our_lines(self):
        self.assertEqual(c.crashes(EMU_LOG, "emu"), [])
        self.assertEqual(len(c.crashes("CRASH: exception 0xc0000005 at f_80123456", "hs")), 1)

    def test_frame_csv_ours_presentmon_and_headerless(self):
        with tempfile.TemporaryDirectory() as d:
            tmp = Path(d)
            ours = write(tmp, "f.csv", "frame,seconds,ms\n1,0.000000,0.000\n2,0.016,16.000\n3,0.080,64.000\n")
            self.assertEqual(c.frame_times(ours), [16.0, 64.0])
            pm = write(tmp, "pm.csv", "Application,ProcessID,MsBetweenPresents\nh.exe,1,16.6\nh.exe,1,33.3\n")
            self.assertEqual(c.frame_times(pm), [16.6, 33.3])
            bare = write(tmp, "b.csv", "1,0.0,0.0\n2,0.016,16.0\n")
            self.assertEqual(c.frame_times(bare), [16.0])

    def test_memory_slope_after_warmup(self):
        pts = [(t, 1000.0 + (5000.0 if t < c.WARMUP_S else t / 60.0)) for t in range(0, 1200, 5)]
        self.assertAlmostEqual(c.slope_per_10min(pts), 10.0, places=3)  # 1 MB a minute
        self.assertIsNone(c.slope_per_10min([(200.0, 1.0), (205.0, 2.0)]))

    def test_notes(self):
        n = c.notes("H grave el agua de Zora negra\nE leve sombra que parpadea\nlinea suelta\nh LEVE algo")
        self.assertEqual(n["H"]["grave"], ["el agua de Zora negra"])
        self.assertEqual(n["H"]["leve"], ["algo"])
        self.assertEqual(n["E"]["leve"], ["sombra que parpadea"])


class Deciding(unittest.TestCase):
    def run_report(self, hs_log: str, notas: str = "", hs_mem: str | None = None, frames: bool = True) -> str:
        with tempfile.TemporaryDirectory() as d:
            tmp = Path(d)
            mem_rows = "t_s,ws_mb,priv_mb,handles\n" + "".join(f"{t},1,{4000 + t / 600.0},1\n" for t in range(0, 1800, 30))
            a = SimpleNamespace(emu_log=write(tmp, "e.txt", EMU_LOG), hs_log=write(tmp, "h.txt", hs_log),
                                emu_frames=None, emu_mem=write(tmp, "em.csv", mem_rows),
                                hs_frames=None, hs_mem=write(tmp, "hm.csv", hs_mem or mem_rows),
                                notas=write(tmp, "n.txt", notas), salida=None, hs_sin_slow_log=not frames)
            return c.report(a)

    def test_move_now_when_every_rule_holds(self):
        text = self.run_report(HS_LOG)
        self.assertTrue(text.startswith("DECISIÓN: MOVER YA"), text)
        self.assertIn("R2 fps: cumple", text)

    def test_wait_on_a_crash(self):
        text = self.run_report(HS_LOG + "\nCRASH: exception 0xc0000005")
        self.assertTrue(text.startswith("DECISIÓN: ESPERAR: no cumple R1 estabilidad"), text)

    def test_wait_on_a_bad_visual_error(self):
        text = self.run_report(HS_LOG, notas="H grave la vegetacion de la Campiña no sale")
        self.assertIn("ESPERAR: no cumple R4 visual", text.splitlines()[0])

    def test_wait_when_not_faster_enough(self):
        slow = HS_LOG.replace("70.0 fps", "58.0 fps").replace("71.0 fps", "58.0 fps").replace("69.0 fps", "58.0 fps") \
            .replace("68.0 fps", "58.0 fps").replace("66.0 fps", "58.0 fps").replace("62.0 fps", "58.0 fps")
        text = self.run_report(slow)
        self.assertIn("R2 fps", text.splitlines()[0])

    def test_both_at_the_cap_hookshot_better_in_the_worst_moments(self):
        capped = HS_LOG
        for v in ("62.0", "66.0", "70.0", "68.0", "71.0"):
            capped = capped.replace(f"({v} fps", "(60.0 fps")
        capped = capped.replace("(69.0 fps", "(55.0 fps")
        text = self.run_report(capped)
        self.assertIn("R2 fps: cumple", text)

    def test_wait_for_data_without_spikes(self):
        text = self.run_report(HS_LOG, frames=False)
        self.assertTrue(text.startswith("DECISIÓN: ESPERAR (faltan datos): R3 tirones"), text)

    def test_wait_on_a_leak(self):
        leak = "t_s,ws_mb,priv_mb,handles\n" + "".join(f"{t},1,{4000 + t / 6.0},1\n" for t in range(0, 1800, 30))
        text = self.run_report(HS_LOG, hs_mem=leak)
        self.assertIn("R5 memoria", text.splitlines()[0])


if __name__ == "__main__":
    unittest.main()
