"""Unit test (no game) for orm_inst.ready_for_games: the RAM and model checks before two games start.
Doc: docs/DECISIONES.md D-R01 (two games, free RAM, local models unloaded)."""
import json
import sys, pathlib

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import orm_inst as oi


class _FakeResponse:
    def __init__(self, body: bytes):
        self._body = body

    def read(self):
        return self._body

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class _FakeOllama:
    def __init__(self, ps_models):
        self.ps_models = ps_models
        self.bodies = []

    def __call__(self, target, *args, **kwargs):
        url = target if isinstance(target, str) else target.full_url
        if url.endswith("/api/ps"):
            return _FakeResponse(json.dumps({"models": [{"name": n} for n in self.ps_models]}).encode())
        if url.endswith("/api/generate"):
            self.bodies.append(json.loads(target.data))
            return _FakeResponse(b"{}")
        raise AssertionError(f"unexpected URL {url}")


def test_free_ram_gb():
    free = oi.free_ram_gb()
    assert isinstance(free, float)
    assert 0 < free < 1024


def test_loaded_models(monkeypatch):
    fake = _FakeOllama(["mine-code:latest", "other-chat:7b"])
    monkeypatch.setattr(oi.urllib.request, "urlopen", fake)
    assert oi.loaded_models() == ["mine-code:latest", "other-chat:7b"]


def test_loaded_models_oserror(monkeypatch):
    def boom(*args, **kwargs):
        raise OSError("no ollama")

    monkeypatch.setattr(oi.urllib.request, "urlopen", boom)
    assert oi.loaded_models() == []


def test_unload_our_models(monkeypatch):
    fake = _FakeOllama(["mine-code:latest", "mine-read:1b", "other-chat:7b", "llama3:8b"])
    monkeypatch.setattr(oi.urllib.request, "urlopen", fake)
    monkeypatch.setattr(oi, "OUR_MODELS", ("mine-",))

    assert oi.unload_our_models() == ["mine-code:latest", "mine-read:1b"]

    assert len(fake.bodies) == 2
    for body in fake.bodies:
        assert body["keep_alive"] == 0
    assert [b["model"] for b in fake.bodies] == ["mine-code:latest", "mine-read:1b"]
    assert not any("other-chat" in b["model"] for b in fake.bodies)
    assert not any(b["model"] == "llama3:8b" for b in fake.bodies)


def test_ready_for_games_still_running(monkeypatch):
    monkeypatch.setattr(oi, "wait_no_instances", lambda *a, **k: False)
    ok, text = oi.ready_for_games()
    assert ok is False
    assert "still running" in text


def test_ready_for_games_ok(monkeypatch):
    monkeypatch.setattr(oi, "wait_no_instances", lambda *a, **k: True)
    monkeypatch.setattr(oi, "unload_our_models", lambda: [])
    monkeypatch.setattr(oi, "free_ram_gb", lambda: 30.0)
    ok, text = oi.ready_for_games()
    assert ok is True
    assert "30.0" in text


def test_ready_for_games_not_enough_ram(monkeypatch):
    monkeypatch.setattr(oi, "wait_no_instances", lambda *a, **k: True)
    monkeypatch.setattr(oi, "loaded_models", lambda: [])
    monkeypatch.setattr(oi, "free_ram_gb", lambda: 2.0)
    monkeypatch.setattr(oi.time, "sleep", lambda *a, **k: None)
    ok, text = oi.ready_for_games(min_free_gb=14, timeout=1)
    assert ok is False
    assert "2.0" in text
