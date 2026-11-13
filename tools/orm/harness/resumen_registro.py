"""Compact timeline of a play session's emulator log (emulator/user/log/eden_log.txt), to read a friend's log fast.

    python -I tools/orm/harness/resumen_registro.py <eden_log.txt> [<other eden_log.txt> ...] [--max 400] [--out f]

Prints, per log: the co-op build, protocol, game build, server and name; which mechanics the log says are on; then one
line per thing that matters, in time order (seconds since the emulator started): connection, joins and leaves, scene
changes, events with their names, puppets created and revealed, pose gaps, downed/revive transitions, chat notices and
the screen line, story cutscenes (solo ones too) and steps, NPC claims and NPC poses, talk flags and their wait, world
lists, shared hearts, the escape, age changes, spectator camera, boss rooms, name tags (camera found), a bad colour,
test marks, and every co-op error or warning. The rest (poses, objects, foliage, rtt lines) is counted, not printed. Only the standard library; reads the log as UTF-8 and never changes it.

Doc: docs/REGISTROS.md (where each log is and what each `coop:` line means). Test: test_resumen_registro.py.
"""
from __future__ import annotations

import argparse
import re
import statistics
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

# "[  10.616987] Core <Info> X:...\coop_hook.cpp:1025:Start: coop: on, ..." (the source path may hold a drive colon)
LINE = re.compile(r"^\[\s*(?P<t>\d+(?:\.\d+)?)\]\s+(?P<cls>\S+)\s+<(?P<lvl>\w+)>\s+"
                  r"(?P<src>.+?\.(?:cpp|hpp|h|inc|c|s)):\d+:(?P<fn>[^:]*):\s?(?P<msg>.*)$")

# Header facts, first match wins.
HEADER = [
    ("build", re.compile(r"coop: build (\S+), protocol (\d+)"), lambda m: f"{m[1]}, protocol {m[2]}"),
    ("game", re.compile(r"coop: game build (\w+)"), lambda m: m[1]),
    ("server", re.compile(r"coop: on, server (.+?), name '(.*)'"), lambda m: f"{m[1]}, name '{m[2]}'"),
    ("player", re.compile(r"coop: welcomed as player (\d+)"), lambda m: f"P{m[1]}"),
]

# Mechanics the log says are on (startup lines).
SWITCHES = [
    ("marionetas", re.compile(r"coop: puppets on")),
    ("hadas", re.compile(r"coop: fairies on")),
    ("objetos", re.compile(r"coop: objects: share on")),
    ("hora", re.compile(r"coop: time shared")),
    ("salas de jefe", re.compile(r"coop: boss rooms shared")),
    ("caído/revivir", re.compile(r"coop: downed: on \(LICHII_COOP_DOWNED\)")),
    ("cinemáticas", re.compile(r"coop: story: on \((?:LICHII_COOP_STORY|LICHII_ORM_STORY_EVENTS)\)")),
    ("espectador", re.compile(r"spectator on \(LICHII_COOP_SPECTATOR\)")),
    ("listas del mundo", re.compile(r"coop: lists: .+: \d+ flags and keys added")),
    ("charlas", re.compile(r"coop: talk: sharing \d+ talk flags")),
    ("pose de PNJ", re.compile(r"coop: npc pose: on \(")),
    ("edad", re.compile(r"coop: age: on \(LICHII_COOP_AGE\)")),
    ("etiquetas", re.compile(r"coop: camera found \(.*\), markers on")),
]
STORY_COUNT = re.compile(r"coop: story: on \((?:LICHII_COOP_STORY|LICHII_ORM_STORY_EVENTS)\): (\d+) shared story events")

# Scene of the player, from the lines that carry it.
SCENE = [
    re.compile(r"coop: you play '.*?' rate \S+ from frame \S+ in (\w+)"),
    re.compile(r"coop: objects: share present \d+ enemies in (\w+)"),
    re.compile(r"coop: drops: asked for the drops of (\w+)"),
    re.compile(r"coop: life: t \S+ life .*?, scene (\w+), at"),
]


def _spawn(m: re.Match) -> str:
    who = "hada" if "Navi" in m[2] or "Fairy" in m[2] else "marioneta"
    return f"{who} de P{m[1]} creada ({m[2]}, {m[3]} ms)"


def _create(m: re.Match) -> str | None:
    value = m[3]
    failed = int(value, 16) == 0 if value.startswith("0x") else int(value) == 0
    return f"FALLO: no se pudo crear {m[2]} de P{m[1]} (returned {value})" if failed else None


def _shown(m: re.Match) -> str:
    """The reveal of a parked puppet; "waited the most" means it came out at the time limit, without its pose."""
    text = f"marioneta de P{m[1]} visible tras {m[2]} ms ({m[3]})"
    return f"AVISO: {text}, sin postura o posición reciente" if "waited the most" in m[4] else text


# Timeline rules: (regex on the message from "coop:" on, text builder). The first rule that matches a line is used;
# a builder that gives None drops the line.
TIMELINE: list[tuple[re.Pattern, object]] = [
    # connection
    (re.compile(r"coop: welcomed as player (\d+) on (.+)"), lambda m: f"conectado como P{m[1]} ({m[2]})"),
    (re.compile(r"coop: player (\d+) '(.*)' joined"), lambda m: f"+ entra P{m[1]} '{m[2]}'"),
    (re.compile(r"coop: player (\d+) '(.*)' left"), lambda m: f"- sale P{m[1]} '{m[2]}'"),
    (re.compile(r"coop: connection lost; reconnecting(.*)"), lambda m: "AVISO: conexión perdida; reconectando"),
    (re.compile(r"coop: rtt first ([\d.]+) ms"), lambda m: f"primer ping {m[1]} ms"),
    (re.compile(r"coop: off(.*)"), lambda m: f"AVISO: co-op apagado{m[1]}"),
    (re.compile(r"coop: .*color must be (.*)"), lambda m: f"FALLO: color mal escrito en coop.ini ({m[1]})"),
    (re.compile(r"coop: screen: connection lost"), lambda m: "AVISO: pantalla: sin conexión, reconectando"),
    (re.compile(r"coop: screen: connection back after (\d+) ms"), lambda m: f"conexión de vuelta tras {m[1]} ms"),
    # chat notices and the screen line (what the player reads)
    (re.compile(r"coop: notice: (.+)"), lambda m: f"chat: {m[1]}"),
    (re.compile(r"coop: downed: screen: '(.*)'"), lambda m: f"pantalla: '{m[1]}'" if m[1] else "pantalla: (vacía)"),
    # events (cutscenes, talks, signs, game over: the game's own names)
    (re.compile(r"coop: event running '(.+?)'"), lambda m: f"evento '{m[1]}'"),
    (re.compile(r"coop: event name '(.+?)' \(was '(.+?)'\)"), lambda m: f"evento sigue como '{m[1]}'"),
    (re.compile(r"coop: event over \(was '(.+?)'\)"), lambda m: f"fin de evento '{m[1]}'"),
    (re.compile(r"coop: event running \(we play"), lambda m: "evento (sin nombre)"),
    (re.compile(r"coop: event over \(we play"), lambda m: "fin de evento (sin nombre)"),
    # puppets
    (re.compile(r"coop: puppet: P(\d+) (\S+) spawned after (\d+) ms"), _spawn),
    (re.compile(r"coop: puppet: P(\d+): create (\S+) requested \(returned (0x[0-9a-fA-F]+|\d+)\)"), _create),
    (re.compile(r"coop: puppet: P(\d+) shown after (\d+) ms \((\w+)\)(.*)"), _shown),
    (re.compile(r"coop: puppet: P(\d+) back after (\d+) ms out of the plan"),
     lambda m: f"marioneta de P{m[1]} vuelve tras {m[2]} ms: escondida hasta tener postura"),
    (re.compile(r"coop: puppet: P(\d+) gone \((.+?)\)"), lambda m: f"marioneta de P{m[1]} desaparece ({m[2]})"),
    (re.compile(r"coop: puppet: P(\d+) stands on our Link after an event: drawn ([\d.]+)"),
     lambda m: f"marioneta de P{m[1]} dibujada {m[2]} m a un lado (encima de tu Link tras un evento)"),
    (re.compile(r"coop: puppet: P(\d+) drawn on its sample again"), lambda m: f"marioneta de P{m[1]} vuelve a su sitio"),
    (re.compile(r"coop: puppet: P(\d+) (show|hide|hold) -> (show|hide|hold)(?: \((.+)\))?"),
     lambda m: f"marioneta de P{m[1]}: {m[2]} -> {m[3]}" + (f" ({m[4]})" if m[4] else "")),
    (re.compile(r"coop: pose: P(\d+) no pose for (\d+) ms"),
     lambda m: f"postura de P{m[1]}: sin posturas {m[2]} ms, se mantiene la última"),
    (re.compile(r"coop: pose: P(\d+) poses back after (\d+) ms"),
     lambda m: f"postura de P{m[1]}: vuelven las posturas tras {m[2]} ms"),
    # downed, revive, group defeat
    (re.compile(r"coop: downed: (\w+) -> (\w+)(.*?)(?: \(life (-?\d+) of (-?\d+).*)?$"),
     lambda m: f"caído: {m[1]} -> {m[2]}{m[3]}" + (f" (vida {m[4]}/{m[5]})" if m[4] else "")),
    (re.compile(r"coop: downed: P(\d+) is down"), lambda m: f"caído: P{m[1]} está caído"),
    (re.compile(r"coop: downed: P(\d+) is up again"), lambda m: f"caído: P{m[1]} se levanta"),
    (re.compile(r"coop: downed: A held ([\d.]+) s beside P(\d+)"), lambda m: f"revivir: A {m[1]} s junto a P{m[2]}"),
    (re.compile(r"coop: downed: REVIVE sent to P(\d+)"), lambda m: f"revivir: REVIVE enviado a P{m[1]}"),
    (re.compile(r"coop: downed: REVIVE from P(\d+) \(the request\)"), lambda m: f"revivir: REVIVE de P{m[1]}"),
    (re.compile(r"coop: downed: group defeat: GROUP_DEFEAT from the server \((.+?)\)"),
     lambda m: f"derrota grupal ({m[1]})"),
    (re.compile(r"coop: downed: gate word .* NOT written"), lambda m: "FALLO: la compuerta de caído no se escribió"),
    # story cutscenes and steps
    (re.compile(r"coop: story: owner: our game started '(.+?)' in (\w+)"),
     lambda m: f"historia: '{m[1]}' empieza aquí (dueño) en {m[2]}"),
    (re.compile(r"coop: story: STORY_EVENT start received from P(\d+) \('(.+?)' in (\w+)\): (.+)"),
     lambda m: f"historia: P{m[1]} empezó '{m[2]}': {m[4]}"),
    (re.compile(r"coop: story: event running '(.+?)' as P(\d+) asked, (\d+) ms after"),
     lambda m: f"historia: '{m[1]}' corre aquí a petición de P{m[2]} ({m[3]} ms)"),
    (re.compile(r"coop: story: '(.+?)' is over here"), lambda m: f"historia: '{m[1]}' acaba aquí"),
    (re.compile(r"coop: story: P(\d+)'s '(.+?)' in (\w+) is over there"),
     lambda m: f"historia: '{m[2]}' acabó en P{m[1]}"),
    (re.compile(r"coop: story: STORY_EVENT played"), lambda m: "historia: el servidor dice ya jugada"),
    (re.compile(r"coop: story: '(.+?)' is solo"),
     lambda m: f"historia: '{m[1]}' es solo (solo la ve quien la empieza; sus banderas viajan)"),
    (re.compile(r"coop: story: '(.+?)' runs here again"), lambda m: f"historia: '{m[1]}' otra vez aquí (no se repite fuera)"),
    (re.compile(r"coop: story: HavePlayedEvent\.(\S+) 0 -> 1"), lambda m: f"historia: vista {m[1]}"),
    (re.compile(r"coop: story: (Step_\w+) (\w+) -> (\w+)"), lambda m: f"paso {m[1]}: {m[2]} -> {m[3]}"),
    (re.compile(r"coop: story: (\w+) 0 -> 1"), lambda m: f"historia: bandera {m[1]} = 1"),
    (re.compile(r"coop: story: (.*(?:not shared|could not|failed).*)"), lambda m: f"FALLO: historia: {m[1]}"),
    # townspeople
    (re.compile(r"coop: npc: talking to (\S+) \S+ \((.+?)\)"), lambda m: f"PNJ: hablando con {m[1]} ({m[2]})"),
    (re.compile(r"coop: npc: talk over, claim let go"), lambda m: "PNJ: fin de la charla, PNJ soltado"),
    (re.compile(r"coop: npc pose: sending (\S+)"), lambda m: f"pose de PNJ: se envía la de {m[1]}"),
    (re.compile(r"coop: npc pose: sending stopped"), lambda m: "pose de PNJ: deja de enviarse"),
    (re.compile(r"coop: npc pose: copies following their owner's pose: (.+)"),
     lambda m: f"pose de PNJ: copias que siguen la del dueño: {m[1]}"),
    (re.compile(r"coop: npc pose: no copy follows a pose now"), lambda m: "pose de PNJ: ninguna copia la sigue"),
    # talk flags (shared_talk.txt) and their wait
    (re.compile(r"coop: talk: sharing (\d+) talk flags \((\d+) new\)"),
     lambda m: f"charlas: {m[1]} banderas compartidas ({m[2]} nuevas)"),
    (re.compile(r"coop: talk: 0x[0-9a-f]+ ?(\S*) (\S+) -> (\S+)(?: in (\w+))?"),
     lambda m: f"charla: {m[1] or '?'} {m[2]} -> {m[3]}" + (f" ({m[4]})" if m[4] else "")),
    (re.compile(r"coop: flags: talk flag 0x[0-9a-f]+ ?(\S*) = (-?\d+) waits: (.+)"),
     lambda m: f"charla: {m[1] or '?'} = {m[2]} espera ({m[3]})"),
    (re.compile(r"coop: flags: talk flag 0x[0-9a-f]+ ?(\S*) = (-?\d+) (written|dropped): ?(.*)"),
     lambda m: f"charla: {m[1] or '?'} = {m[2]} " + ("escrita" if m[3] == "written" else "descartada")
     + (f" ({m[4]})" if m[4] else "")),
    # world lists per block (shared_dungeon_child.txt, shared_dungeon_adult.txt)
    (re.compile(r"coop: lists: (LICHII_COOP_DUNGEON_LISTS .*)"), lambda m: f"FALLO: listas: {m[1]}"),
    (re.compile(r"coop: lists: (.+?): (\d+) flags and keys added, (\d+) taken off"),
     lambda m: f"listas: {Path(m[1].replace(chr(92), '/')).name}: {m[2]} añadidas, {m[3]} quitadas"),
    # shared hearts, the escape from Ganon's castle, age changes (D-R17)
    (re.compile(r"coop: hearts: (.+)"), lambda m: f"corazones: {m[1]}"),
    (re.compile(r"coop: escape: (.+)"), lambda m: f"huida: {m[1]}"),
    (re.compile(r"coop: age: (.+)"), lambda m: f"edad: {m[1]}"),
    # spectator camera
    (re.compile(r"coop: spectator: following P(\d+) \((\w+)\)"), lambda m: f"espectador: sigue a P{m[1]} ({m[2]})"),
    (re.compile(r"coop: spectator: released"), lambda m: "espectador: suelta la cámara"),
    (re.compile(r"coop: spectator: (the camera did not wake.*)"), lambda m: f"AVISO: espectador: {m[1]}"),
    # boss rooms
    (re.compile(r"coop: boss: (phase \S+, scene \w+)"), lambda m: f"jefe: {m[1]}"),
    (re.compile(r"coop: boss: (.*(?:beaten|starts over|asked the scene change).*)"), lambda m: f"jefe: {m[1]}"),
    (re.compile(r"coop: boss: respawn: (.+)"), lambda m: f"jefe: vuelta a la sala: {m[1]}"),
    (re.compile(r"coop: boss: (.*moving in \(try \d+\).*)"), lambda m: f"jefe: {m[1]}"),
    # harness test marks (only in our test runs)
    (re.compile(r"coop: test mark (\S+)"), lambda m: f"marca de prueba '{m[1]}'"),
]

RTT = re.compile(r"coop: rtt median ([\d.]+) ms, min ([\d.]+) ms, max ([\d.]+) ms over (\d+) pings(?: \(transport (\w+)\))?")
NOISE = re.compile(r"coop: (\w+)")  # the subsystem of a line that is only counted


@dataclass
class Resumen:
    name: str
    lines: int = 0
    header: dict[str, str] = field(default_factory=dict)
    switches: list[str] = field(default_factory=list)
    story_events: int | None = None
    timeline: list[list] = field(default_factory=list)  # [t, text, repeats]
    rtt: list[tuple[float, float, float, str]] = field(default_factory=list)
    counted: Counter = field(default_factory=Counter)
    emulator_errors: Counter = field(default_factory=Counter)
    last_t: float = 0.0


def _add(r: Resumen, t: float, text: str) -> None:
    """Appends a timeline line; the same text right after the last one only counts a repeat."""
    if r.timeline and r.timeline[-1][1] == text:
        r.timeline[-1][2] += 1
        return
    r.timeline.append([t, text, 1])


def parse(lines, name: str = "eden_log.txt") -> Resumen:
    r = Resumen(name=name)
    scene = None
    for raw in lines:
        r.lines += 1
        m = LINE.match(raw.rstrip("\r\n"))
        if not m:
            continue
        t, lvl, msg = float(m["t"]), m["lvl"], m["msg"]
        r.last_t = t
        at = msg.find("coop:")
        if at < 0:
            if lvl in ("Error", "Critical"):
                r.emulator_errors[Path(m["src"].replace("\\", "/")).name] += 1
            continue
        c = msg[at:]
        for key, rx, build in HEADER:
            if key not in r.header:
                hm = rx.search(c)
                if hm:
                    r.header[key] = build(hm)
        for label, rx in SWITCHES:
            if label not in r.switches and rx.search(c):
                r.switches.append(label)
        sm = STORY_COUNT.search(c)
        if sm and r.story_events is None:
            r.story_events = int(sm[1])
        for rx in SCENE:
            sc = rx.search(c)
            if sc and sc[1] != scene:
                scene = sc[1]
                _add(r, t, f"escena {scene}")
                break
        rm = RTT.search(c)
        if rm:
            r.rtt.append((float(rm[1]), float(rm[2]), float(rm[3]), rm[5] or "?"))
            continue
        text = None
        for rx, build in TIMELINE:
            tm = rx.search(c)
            if tm:
                text = build(tm)
                break
        if text is None and lvl in ("Error", "Critical", "Warning"):
            text = ("ERROR: " if lvl != "Warning" else "AVISO: ") + c
        if text is not None:
            _add(r, t, text)
        else:
            sub = NOISE.match(c)
            r.counted[sub[1] if sub else "?"] += 1
    return r


def render(r: Resumen, max_lines: int = 400) -> str:
    out = [f"== {r.name}: {r.lines} líneas, {r.last_t:.0f} s de emulador"]
    for key, label in (("build", "co-op"), ("game", "juego"), ("server", "servidor"), ("player", "yo")):
        out.append(f"   {label}: {r.header.get(key, '?')}")
    on = list(r.switches)
    if r.story_events is not None and "cinemáticas" in on:
        on[on.index("cinemáticas")] = f"cinemáticas ({r.story_events} en la lista)"
    out.append("   activo: " + (", ".join(on) if on else "nada de co-op en el registro"))
    if r.rtt:
        meds = [x[0] for x in r.rtt]
        out.append(f"   ping: mediana {statistics.median(meds):.0f} ms (de {len(r.rtt)} medidas), "
                   f"mín {min(x[1] for x in r.rtt):.0f}, máx {max(x[2] for x in r.rtt):.0f} ms, "
                   f"transporte {r.rtt[-1][3]}")
    problems = sum(1 for _, s, _n in r.timeline if s.startswith(("ERROR", "FALLO", "AVISO")))
    out.append(f"   problemas de co-op: {problems}")
    out.append("")
    shown = r.timeline if len(r.timeline) <= max_lines else r.timeline[:max_lines]
    for t, s, n in shown:
        out.append(f"{t:9.1f}  {s}" + (f" (x{n})" if n > 1 else ""))
    if len(r.timeline) > max_lines:
        out.append(f"   ... {len(r.timeline) - max_lines} líneas más (--max)")
    if r.counted:
        top = ", ".join(f"{k} {v}" for k, v in r.counted.most_common(8))
        out.append(f"\n   contadas sin imprimir (coop: <tema>): {top}")
    if r.emulator_errors:
        top = ", ".join(f"{k} {v}" for k, v in r.emulator_errors.most_common(4))
        out.append(f"   errores del emulador (no co-op, casi siempre ruido): {sum(r.emulator_errors.values())}: {top}")
    return "\n".join(out)


def summarize(path: Path, max_lines: int = 400) -> str:
    with path.open(encoding="utf-8", errors="replace") as f:
        return render(parse(f, path.name), max_lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("logs", nargs="+", type=Path, help="eden_log.txt files (one per player)")
    ap.add_argument("--max", type=int, default=400, help="timeline lines per log (default 400)")
    ap.add_argument("--out", type=Path, help="write the summary here as well as on screen")
    a = ap.parse_args(argv)
    parts = []
    for p in a.logs:
        if not p.is_file():
            parts.append(f"== {p}: no existe")
            continue
        parts.append(summarize(p, a.max))
    text = "\n\n".join(parts) + "\n"
    if a.out:
        a.out.write_text(text, encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
