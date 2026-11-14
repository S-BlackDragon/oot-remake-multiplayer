# Limits: what we can and cannot do, and where we are

**English** | [Español](es/LIMITS.md)

I am S-BlackDragon. I know Ocarina of Time well, I play your co-op with my friends, and I am **not a programmer**: I
work with AI agents ("we" in these documents is me and them). This page says honestly what our setup allows, why
testing is slow, and how far we are.

## Our setup

| What | Detail |
|---|---|
| One home PC | Ryzen 9 7900X (24 threads), 31 GB RAM, RTX 4060 Ti 8 GB |
| Used for | everything: builds, tests, and my own daily use and play |
| Other machines | one more machine that runs other services in production; we never use it for builds or games |
| Cloud | none (we looked at GPU virtual machines and dropped the idea) |

## Why validation is slow

- **Each test needs two hidden games at once.** One pair takes about 14-15 GB of RAM and 10-14 cores. So only **one
  pair fits** on this PC: with a pair running, free RAM goes under 1 GB. Two pairs at once, or a pair plus a build, do
  not fit.
- **Each scenario takes 5 to 10 minutes** (start the server and two games, load, play the script, read the logs).
- **A full pass is about 50 hours.** Our plan needs about 429 scenarios; at that pace, one full pass is roughly 50
  hours of continuous runs. Hot instances (one pair plays a chain of scenarios) help, but not by an order of magnitude.
- **Tests stop while I play.** It is the same PC: a `STOP` file pauses everything when I sit down to play.
- **Some moments cannot be reached by synthetic saves** (Epona, the final escape, some boss states). Those only show up
  in real play.
- **Six players are only tested with bots** (one game plus five fake players). Real six-player sessions depend on my
  friends' free time.
- **Analysis and fixes are limited too.** I work with AI agents within a usage budget, so the time to read logs and fix
  things is limited, not only the machine time.

## Where we are (progress meter, 2026-11-13)

From our progress meter `avance.py` (not in this gift: it reads our private notes), run on 2026-11-13. The meter counts 145 boxes (20 content blocks, up to 8 boxes
each; see [HOW-WE-WORK.md](HOW-WE-WORK.md)).

| Figure | % | Boxes |
|---|---|---|
| **Verified in game** (scenarios green with all their checks, two real games) | **1.3 %** | 1.9 of 145 |
| **Code ready** (integrated and documented, not yet played) | **51.6 %** | 74.8 of 145 |
| Waiting for a value from a game (integrated, but needs a probe's value) | 6.7 % | 9.7 of 145 |
| **Scenarios written** (exist and have `#! expect` checks) | **81.7 %** | 118.4 of 145 |

In scenarios: **10 green out of 429 needed.**

So most of the code exists, most of the scenarios exist, and very little has passed in a real game. The gap is test
time, not ideas.

## Estimated time to a co-op tested 100 % end to end

At this pace, roughly **8 to 12 weeks**. It is bound mostly by test throughput (one pair at a time, about 50 hours per
full pass) and by fixing the families of failures that the first full pass will reveal. I will revise this estimate
after that first full pass. Real play sessions with friends speed it up, because they reach moments that scripts and
synthetic saves cannot.

## What would speed it up (for anyone reading)

- **More RAM, or a second PC**, so two or more pairs of games can run in parallel.
- **Real players reporting with logs**: a game log from a real session shows in minutes what a script may take days to
  reach.
