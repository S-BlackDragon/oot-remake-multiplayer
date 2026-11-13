"""Unit test (no game) for aguante.py: the log/relay/TCP parsing, the scenario chain and the verdict math, with fake data.
Doc: docs/AGUANTE.md."""
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import aguante as ag


# ------------------------------------------------------------------ parsing

def test_logtail_reads_only_new_bytes_and_waits_for_whole_lines(tmp_path):
    log = tmp_path / "eden_log.txt"
    log.write_bytes(b"[  1.0] Frontend <Info> FPS: 30.0 speed: 100% frametime: 33.3 ms\n"
                    b"[  2.0] Core <Info> coop: welcomed as player 1 on 127.0.0.1:27580\n"
                    b"[  3.0] Core <Info> coop: test mark sta")
    t = ag.LogTail(log)
    assert t.poll() == 2
    assert t.fps == [30.0] and t.welcomed == 1 and t.marks == {}
    with open(log, "ab") as f:
        f.write(b"rt\r\n[  4.0] Frontend <Info> FPS: 28.5 speed: 95% frametime: 35 ms\n"
                b"[  5.0] Core <Warning> coop: connection lost; reconnecting (nothing from the server for 0 ms)\n"
                b"[  6.0] Core <Critical> boom\n")
    t.begin_window()
    assert t.poll() == 4
    assert t.marks == {"start": 1}
    assert t.take_fps() == [30.0, 28.5] and t.fps == []
    assert t.lost == 1 and t.critical == 1
    assert "coop: test mark start" in t.end_window() and t.window is None
    assert t.poll() == 0


def test_logtail_starts_again_on_a_new_log(tmp_path):
    log = tmp_path / "l.txt"
    log.write_text("FPS: 30.0 speed: 100%\n" * 10)
    t = ag.LogTail(log)
    t.poll()
    log.write_text("FPS: 12.0 speed: 50%\n")
    t.poll()
    assert t.fps[-1] == 12.0 and len(t.fps) == 11


def test_last_server_stats():
    text = ("orm_server: stats; players 2, relayed 10, dropped 0, ws replaced 0, ws flooded 0\n"
            "something else\n"
            "orm_server: stats; players 1, relayed 99, dropped 3, ws replaced 0, ws flooded 0\n")
    assert ag.last_server_stats(text) == {"players": 1, "relayed": 99, "dropped": 3}
    assert ag.last_server_stats("orm_server: listening") is None


def _row(state, lport, rport, pid):
    be = lambda p: struct.unpack("<I", struct.pack(">H", p) + b"\0\0")[0]  # network order in the low 16 bits
    return struct.pack("<6I", state, 0x0100007F, be(lport), 0x0100007F, be(rport), pid)


def test_tcp_rows_and_established_count():
    buf = struct.pack("<I", 5) + b"".join([
        _row(5, 27581, 50001, 42),   # A at the relay
        _row(5, 27581, 50002, 42),   # B (through the proxy) at the relay
        _row(2, 27581, 0, 42),       # the listener
        _row(5, 50003, 27581, 7),    # someone's client side towards the relay
        _row(8, 27581, 50004, 42),   # CLOSE_WAIT: only established ones count
    ])
    rows = ag.tcp_rows(buf)
    assert rows[0] == (5, 27581, 50001, 42)
    assert ag.count_established(rows, 27581, 42) == 2
    assert ag.count_established(rows, 27581) == 2
    assert ag.count_established(rows, 27581, 9) == 0
    assert ag.tcp_rows(b"") == [] and ag.tcp_rows(struct.pack("<I", 3)) == []  # truncated table


def test_load_list_and_classify(tmp_path):
    assert ag.load_list("# head\nwalk\n\n pose  # comment\nwalk\n") == ["walk", "pose", "walk"]
    (tmp_path / "walk.A.txt").write_text("mark start\nstick 0 1 60\nmark end\n")
    (tmp_path / "boss.A.txt").write_text("#! noload\nmark start\nwarp X Y\npress A\n")
    (tmp_path / "quiet.A.txt").write_text("wait 60\n")
    ok, skipped = ag.classify(["walk", "boss", "quiet", "gone"], tmp_path)
    assert ok == ["walk"]
    assert [s.split()[0] for s in skipped] == ["boss", "quiet", "gone"]


def test_csv_round_trip(tmp_path):
    rows = [{"t_min": 0.0, "A_alive": 1, "A_fps_med": None, "A_priv_mb": 6000.123, "srv_conns": 2},
            {"t_min": 5.0, "A_alive": 1, "A_fps_med": 30.0, "A_priv_mb": 6001.0, "srv_conns": 2}]
    ag.write_csv(tmp_path / "m.csv", rows)
    back = ag.read_csv(tmp_path / "m.csv")
    assert back[0]["A_fps_med"] is None and back[0]["A_priv_mb"] == 6000.12
    assert back[1]["A_fps_med"] == 30.0 and back[1]["B_priv_mb"] is None


def test_lock_and_stop(tmp_path):
    lock, stop = tmp_path / "game.lock", tmp_path / "STOP"
    assert ag.take_lock(lock, stop, 0, note=lambda s: None) == "taken"
    assert (lock / "owner.txt").exists()
    assert ag.take_lock(lock, stop, 0, note=lambda s: None) == "timeout"  # someone has it
    ag.give_lock(lock)
    stop.write_text("")
    assert ag.take_lock(lock, stop, 0, note=lambda s: None) == "timeout"  # STOP holds the start
    assert not lock.exists()


# ------------------------------------------------------------------ the chain

class _Tail:
    def __init__(self):
        self.marks, self.lines, self.window = {}, [], None

    def begin_window(self):
        self.window = []

    def end_window(self):
        text, self.window = "\n".join(self.window or []), None
        return text


def test_chain_waits_for_the_last_mark_and_cycles(tmp_path):
    (tmp_path / "walk.A.txt").write_text("#! expect coop: walked\nmark start\nmark end\n")
    (tmp_path / "pose.A.txt").write_text("mark a\nmark b\n")
    (tmp_path / "pose.B.txt").write_text("mark bb\n")
    scripts = {g: tmp_path / f"{g}.txt" for g in ag.GAMES}
    tails = {g: _Tail() for g in ag.GAMES}
    c = ag.Chain(["walk", "pose"], tmp_path, scripts, tails, timeout=100, gap=5)
    c.poll(0)
    assert scripts["A"].read_text().startswith("# aguante round 1: walk") and "aguante-idle" in scripts["B"].read_text()
    c.poll(10)
    assert c.results == []
    tails["A"].marks["end"] = 1
    tails["A"].window.append("coop: walked")
    c.poll(11)
    assert c.results[-1] == {"name": "walk", "ok": True, "seconds": 11.0, "expects": 1, "missing": 0}
    c.poll(12)  # the gap
    assert c.cur is None
    c.poll(16)
    assert c.cur["name"] == "pose" and set(c.cur["wait"]) == {"A", "B"}
    tails["A"].marks["b"] = 1
    c.poll(20)
    assert c.cur is not None  # B's last mark still missing
    c.poll(200)  # timeout
    assert c.results[-1]["ok"] is False
    c.poll(205)
    assert c.cur["name"] == "walk" and c.cur["wait"]["A"] == ("end", 1)  # round 3 waits for a second 'end'
    s = c.summary(["boss (loads the save itself)"])
    assert s["rounds"] == 2 and s["missed"] == 1 and s["missed_names"] == ["pose"] and s["per_name"] == {"walk": 1, "pose": 1}


# ------------------------------------------------------------------ the verdict

META = {"minutes": 120, "sample_every": 5, "warmup": 10, "restart_at": 30, "cut_at": 60, "reconnect_timeout": 120,
        "games": ["A", "B"], "scenarios": {"rounds": 140, "missed": 0, "expects": 10, "expects_missing": 2}}
EVENTS = [{"kind": "relay_restart", "t_min": 30.0, "ok": True, "detail": "A back in 4 s, B back in 6 s"},
          {"kind": "net_cut", "t_min": 60.0, "ok": True, "detail": "20 s cut; B back in 5 s; A kept its connection"}]


def rows(n=25, priv=lambda g, t: 6000.0, handles=lambda g, t: 3000.0, fps=lambda g, t: 30.0, srv=lambda t: 12.0,
         conns=lambda t: 2):
    out = []
    for i in range(n):
        t = i * 5.0
        r = {"t_min": t, "srv_alive": 1, "srv_rss_mb": srv(t), "srv_conns": conns(t), "srv_players": 2}
        for g in "AB":
            r.update({f"{g}_alive": 1, f"{g}_fps_med": fps(g, t) if t else None, f"{g}_fps_min": 20.0,
                      f"{g}_fps_n": 150 if t else 0, f"{g}_ws_mb": 5000.0, f"{g}_priv_mb": priv(g, t),
                      f"{g}_handles": handles(g, t), f"{g}_log_mb": 1 + t * 0.5})
        out.append(r)
    return out


def test_theil_sen_is_exact_on_a_line_and_ignores_one_spike():
    pts = [(t, 100 + 2 * t) for t in range(10, 121, 5)]
    assert abs(ag.theil_sen(pts) - 2) < 1e-9
    pts[5] = (pts[5][0], 5000)  # one load peak
    assert abs(ag.theil_sen(pts) - 2) < 0.2
    assert ag.theil_sen([(1, 1)]) is None


def test_flat_run_passes():
    word, lines = ag.analyze(rows(), EVENTS, META)
    assert word == "PASS", lines
    assert any(l.startswith("A private MB") and l.rstrip().endswith("ok") for l in lines)


def test_private_bytes_leak_fails_and_slow_growth_warns():
    word, lines = ag.analyze(rows(priv=lambda g, t: 6000 + (6 * t if g == "B" else 0)), EVENTS, META)  # 360 MB/h
    assert word == "FAIL" and any("FAIL: B private MB grows +360.0/h" in l for l in lines)
    word, lines = ag.analyze(rows(priv=lambda g, t: 6000 + 2 * t), EVENTS, META)  # 120 MB/h
    assert word == "WARN" and any("WARN: A private MB grows +120.0/h" in l for l in lines)


def test_handle_leak_fails():
    word, lines = ag.analyze(rows(handles=lambda g, t: 3000 + 20 * t), EVENTS, META)  # 1200/h
    assert word == "FAIL" and any("A handles grows" in l for l in lines)


def test_warmup_is_left_out():
    # a big rise in the first 10 min (the world load), flat after: no leak
    word, lines = ag.analyze(rows(priv=lambda g, t: 4000.0 if t < 10 else 6000.0), EVENTS, META)
    assert word == "PASS", lines


def test_relay_rss_before_the_restart_is_not_a_trend():
    # the new relay starts small at minute 30 and stays flat: only the samples from minute 35 count
    word, lines = ag.analyze(rows(srv=lambda t: 40.0 if t < 30 else 8.0), EVENTS, META)
    assert word == "PASS", lines
    word, lines = ag.analyze(rows(srv=lambda t: 8.0 + t * 0.5), EVENTS, META)  # 30 MB/h
    assert word == "FAIL" and any("relay RSS MB grows" in l for l in lines)


def test_frozen_and_falling_fps():
    word, lines = ag.analyze(rows(fps=lambda g, t: 3.0 if (g == "A" and t == 70) else 30.0), EVENTS, META)
    assert word == "FAIL" and any("A frozen" in l and "minute 70" in l for l in lines)
    word, lines = ag.analyze(rows(fps=lambda g, t: 30.0 - (t / 120) * 12 if g == "B" else 30.0), EVENTS, META)
    assert word == "WARN" and any("B fps" in l and "lower at the end" in l for l in lines)
    assert ag.fps_drop([30, 30]) is None and round(ag.fps_drop([30, 30, 30, 15, 15, 15])) == 50


def test_failed_reconnect_and_dead_game_fail():
    ev = [dict(EVENTS[0], ok=False, detail="A back in 4 s; not back in 120 s: B"), EVENTS[1]]
    word, lines = ag.analyze(rows(), ev, META)
    assert word == "FAIL" and any("relay_restart at minute 30.0" in l for l in lines)
    r = rows()
    r[-1]["B_alive"] = 0
    word, lines = ag.analyze(r, EVENTS, META)
    assert word == "FAIL" and any("game B not running" in l for l in lines)


def test_skipped_cut_is_not_a_fail_but_a_missing_one_warns():
    ev = [EVENTS[0], {"kind": "net_cut", "t_min": 60.0, "ok": None, "detail": "no netsim"}]
    assert ag.analyze(rows(), ev, META)[0] == "PASS"
    word, lines = ag.analyze(rows(), [EVENTS[0]], META)
    assert word == "WARN" and any("net_cut planned at minute 60 not done" in l for l in lines)


def test_ghost_connection_and_end_counts_warn():
    word, lines = ag.analyze(rows(conns=lambda t: 3 if t == 90 else 2), EVENTS, META)
    assert word == "WARN" and any("more connections than games" in l for l in lines)
    word, lines = ag.analyze(rows(conns=lambda t: 1 if t == 120 else 2), EVENTS, META)
    assert word == "WARN" and any("WebSocket connections at the end 1" in l for l in lines)


def test_stopped_and_short_runs():
    meta = dict(META, stopped="STOP file at minute 40.2")
    word, lines = ag.analyze(rows(n=9), EVENTS[:1], meta)
    assert word == "STOPPED" and any(l.startswith("VERDICT: STOPPED (STOP file") for l in lines)
    word, lines = ag.analyze(rows(n=3), [], dict(META, minutes=10, restart_at=0, cut_at=0))
    assert word == "PASS" and any("n/a" in l for l in lines) and any("under 60 min" in l for l in lines)


def test_reanalizar_rewrites_the_summary(tmp_path):
    ag.write_csv(tmp_path / "muestras.csv", rows())
    (tmp_path / "eventos.json").write_text(json.dumps(EVENTS))
    (tmp_path / "meta.json").write_text(json.dumps(META))
    (tmp_path / "resumen.txt").write_text("head\n== analysis\nold\n")
    assert ag.main(["--reanalizar", str(tmp_path)]) == 0
    text = (tmp_path / "resumen.txt").read_text()
    assert text.startswith("head\n== analysis (again)") and "VERDICT: PASS" in text and "old" not in text


def test_chain_reads_the_unified_directives(tmp_path):
    # never / count / within and a letter for the other game, as r03_pareja reads them; a bots line is left out
    (tmp_path / "s.A.txt").write_text("#! never coop: lost\n#! count B coop: hi >= 2 within m1..\n"
                                      "#! expect bots joined\nmark m1\nmark end\n")
    scripts = {g: tmp_path / f"{g}.txt" for g in ag.GAMES}
    tails = {g: _Tail() for g in ag.GAMES}
    c = ag.Chain(["s"], tmp_path, scripts, tails, timeout=100, gap=5)
    c.poll(0)
    assert [d["kind"] for d in c.cur["expects"]] == ["never", "count"]
    tails["A"].window += ["coop: lost"]
    tails["B"].window += ["coop: hi", "coop: test mark m1", "coop: hi", "coop: hi"]
    tails["A"].marks["end"] = 1
    c.poll(3)
    assert c.results[-1] == {"name": "s", "ok": True, "seconds": 3.0, "expects": 2, "missing": 1}
