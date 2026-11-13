// ORM (oot-remake docs/RIESGOS_RESUELTOS.md, "Red, kit y alojamiento"): a
// connection that cannot keep up is told in Spanish, never degraded silently;
// a link that keeps up says nothing. LINK_NOTICE (type 253) carries the
// relay's view of the downlink and, before a REFUSED wrong_protocol_version,
// the server's protocol.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "coop/host/orm_link_watch.hpp"
#include "coop/protocol/codec.hpp"

namespace {

namespace proto = coop::protocol;
using coop::host::LinkAdvice;
using coop::host::LinkSample;
using coop::host::LinkWatch;
using coop::host::LinkWatchConfig;

LinkSample up(std::uint64_t now_ms, std::uint32_t queued = 0, std::uint64_t dropped = 0) {
  LinkSample s;
  s.now_ms = now_ms;
  s.connected = true;
  s.send_queued_bytes = queued;
  s.send_dropped_frames = dropped;
  return s;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Every advice from 0 to `until_ms`, one sample each 250 ms from `make`.
template <typename Make>
std::vector<LinkAdvice> run(LinkWatch& w, std::uint64_t from_ms, std::uint64_t until_ms, Make make) {
  std::vector<LinkAdvice> all;
  for (std::uint64_t t = from_ms; t <= until_ms; t += 250) {
    for (auto& a : w.sample(make(t))) all.push_back(std::move(a));
  }
  return all;
}

}  // namespace

TEST_CASE("link watch: a link that keeps up says nothing, even with a join burst and a few drops") {
  LinkWatch w;
  // A join: 600 KiB waiting for one second, then gone; 10 frames dropped in
  // five seconds (below 15); then a quiet minute.
  const auto all = run(w, 0, 60'000, [](std::uint64_t t) {
    const std::uint32_t queued = t < 1000 ? 600u << 10 : 4096;
    const std::uint64_t dropped = t < 5000 ? t / 500 : 10;
    return up(t, queued, dropped);
  });
  CHECK(all.empty());
  CHECK_FALSE(w.uplink_behind());
}

TEST_CASE("link watch: uplink drops warn once in Spanish, then fine again after the calm time") {
  LinkWatch w;
  std::uint64_t dropped = 0;
  // 20 seconds of 10 drops a second, then none.
  const auto all = run(w, 0, 40'000, [&](std::uint64_t t) {
    if (t > 0 && t <= 20'000) dropped += 2;  // 8 a second at 4 samples a second
    return up(t, 8192, dropped);
  });
  REQUIRE(all.size() == 2);
  CHECK(all[0].warning);
  CHECK(contains(all[0].log, "coop: link: our uplink cannot keep up"));
  CHECK(contains(all[0].notice, "no da abasto para enviar"));
  CHECK(contains(all[0].notice, "por cable"));
  CHECK_FALSE(all[1].warning);
  CHECK(contains(all[1].log, "keeps up again"));
  CHECK(contains(all[1].notice, "vuelve a ir bien"));
  CHECK_FALSE(w.uplink_behind());
}

TEST_CASE("link watch: bytes held high for three seconds warn, a shorter burst does not") {
  LinkWatchConfig c;
  LinkWatch w(c);
  CHECK(run(w, 0, 2500, [](std::uint64_t t) { return up(t, 300u << 10); }).empty());
  CHECK(run(w, 2750, 10'000, [](std::uint64_t t) { return up(t, 1000); }).empty());
  const auto all = run(w, 10'250, 14'000, [](std::uint64_t t) { return up(t, 300u << 10); });
  REQUIRE(all.size() == 1);
  CHECK(contains(all[0].log, "300 KiB waiting"));
  CHECK_FALSE(all[0].notice.empty());
}

TEST_CASE("link watch: UDP loss held high warns with its own line, on WebSocket loss is not looked at") {
  {
    LinkWatch w;
    const auto all = run(w, 0, 8000, [](std::uint64_t t) {
      LinkSample s = up(t);
      s.has_loss = true;
      s.loss_percent = 35;
      return s;
    });
    REQUIRE(all.size() == 1);
    CHECK(contains(all[0].notice, "pierde muchos paquetes (35 %)"));
  }
  {
    LinkWatch w;
    const auto all = run(w, 0, 8000, [](std::uint64_t t) {
      LinkSample s = up(t);
      s.has_loss = false;
      s.loss_percent = 35;
      return s;
    });
    CHECK(all.empty());
  }
}

TEST_CASE("link watch: a flapping uplink logs each change but tells the player once a minute") {
  LinkWatch w;
  std::uint64_t dropped = 0;
  std::vector<LinkAdvice> all;
  // Bad for 2 s, then good for 20 s (the drops stay in the 5 s window, then
  // 10 s of calm), five times: bad from 0, 22, 44, 66 and 88 s.
  for (int round = 0; round < 5; ++round) {
    const std::uint64_t base = static_cast<std::uint64_t>(round) * 22'000;
    auto part = run(w, base, base + 21'750, [&](std::uint64_t t) {
      if (t - base < 2000) dropped += 5;
      return up(t, 0, dropped);
    });
    all.insert(all.end(), part.begin(), part.end());
  }
  int warnings = 0;
  int told = 0;
  int fine_told = 0;
  for (const auto& a : all) {
    if (a.warning) {
      ++warnings;
      if (!a.notice.empty()) ++told;
    } else if (!a.notice.empty()) {
      ++fine_told;
    }
  }
  CHECK(warnings == 5);
  CHECK(told == 2);       // at 0.75 s and 66.75 s: the others are within a minute of a told one
  CHECK(fine_told == 2);  // "fine again" only after a told one
  CHECK(all.size() == 10);
}

TEST_CASE("link watch: not connected resets the uplink watch, a new transport's drop count is not a drop") {
  LinkWatch w;
  std::uint64_t dropped = 0;
  auto all = run(w, 0, 6000, [&](std::uint64_t t) {
    dropped += 3;
    return up(t, 0, dropped);
  });
  REQUIRE(all.size() == 1);
  CHECK(w.uplink_behind());
  LinkSample off;
  off.now_ms = 6250;
  const auto reset = w.sample(off);
  REQUIRE(reset.size() == 1);
  CHECK(reset[0].notice.empty());
  CHECK_FALSE(w.uplink_behind());
  // A new client starts its count at 0: never "minus a lot" frames dropped.
  CHECK(run(w, 6500, 20'000, [](std::uint64_t t) { return up(t, 0, 0); }).empty());
}

TEST_CASE("link watch: the relay's behind and caught_up notices, in Spanish, the back one only after the told one") {
  LinkWatch w;
  auto a = w.notice(proto::LinkNotice{proto::LinkNoticeKind::behind, 37, 412}, 1000);
  REQUIRE(a.size() == 1);
  CHECK(a[0].warning);
  CHECK(contains(a[0].log, "drops 37% of the frames for us (412 KiB"));
  CHECK(contains(a[0].notice, "no da abasto para recibir a todos (se pierde el 37 %"));
  CHECK(w.downlink_behind());
  // A second one within the minute: logged, not told again.
  a = w.notice(proto::LinkNotice{proto::LinkNoticeKind::behind, 50, 500}, 20'000);
  REQUIRE(a.size() == 1);
  CHECK(a[0].notice.empty());
  a = w.notice(proto::LinkNotice{proto::LinkNoticeKind::caught_up, 25, 0}, 30'000);
  REQUIRE(a.size() == 1);
  CHECK(contains(a[0].log, "behind for 25 s"));
  CHECK(contains(a[0].notice, "vuelve a ir bien"));
  CHECK_FALSE(w.downlink_behind());
  // caught_up with nothing told before: logged only.
  a = w.notice(proto::LinkNotice{proto::LinkNoticeKind::caught_up, 3, 0}, 31'000);
  REQUIRE(a.size() == 1);
  CHECK(a[0].notice.empty());
  // A kind from a newer relay: one log line, nothing for the player.
  a = w.notice(proto::LinkNotice{static_cast<proto::LinkNoticeKind>(9), 1, 2}, 32'000);
  REQUIRE(a.size() == 1);
  CHECK(contains(a[0].log, "unknown to this build"));
  CHECK(a[0].notice.empty());
}

TEST_CASE("link watch: the server's over_rate notice is told every time it comes (the server keeps it to once a minute)") {
  LinkWatch w;
  auto a = w.notice(proto::LinkNotice{proto::LinkNoticeKind::over_rate, 520, 0}, 1000);
  REQUIRE(a.size() == 1);
  CHECK(a[0].warning);
  CHECK(contains(a[0].log, "dropped 520 of our samples or poses and 0 reliable"));
  CHECK(contains(a[0].notice, "520 movimientos)"));
  CHECK_FALSE(contains(a[0].notice, "cambios de la partida"));
  a = w.notice(proto::LinkNotice{proto::LinkNoticeKind::over_rate, 3, 80}, 2000);
  REQUIRE(a.size() == 1);
  CHECK(contains(a[0].notice, "80 cambios de la partida"));
  CHECK(contains(a[0].notice, "algún cambio no les haya llegado"));
}

TEST_CASE("link watch: refusals in Spanish, with both protocols when the relay said its own") {
  using proto::RefuseReason;
  const auto old_server = coop::host::refused_notice(RefuseReason::wrong_protocol_version, 108, 109);
  CHECK(contains(old_server, "protocolo 108; este juego, el 109"));
  CHECK(contains(old_server, "el que hospeda tiene que actualizar"));
  const auto new_server = coop::host::refused_notice(RefuseReason::wrong_protocol_version, 110, 109);
  CHECK(contains(new_server, "Tu kit es más viejo"));
  const auto unknown = coop::host::refused_notice(RefuseReason::wrong_protocol_version, std::nullopt, 109);
  CHECK(contains(unknown, "este juego habla el protocolo 109"));
  CHECK(contains(unknown, "VERSION.txt"));
  CHECK(contains(coop::host::refused_notice(RefuseReason::bad_password, std::nullopt, 109), "Contraseña"));
  CHECK(contains(coop::host::refused_notice(RefuseReason::server_full, std::nullopt, 109), "caben 6"));
  CHECK(contains(coop::host::refused_notice(RefuseReason::name_taken, std::nullopt, 109), "play.cmd change"));
  CHECK(contains(coop::host::refused_notice(static_cast<RefuseReason>(42), std::nullopt, 109), "motivo 42"));
}

TEST_CASE("link notice: type 253, channel 0, server only, frozen bytes, unknown kinds kept") {
  CHECK(static_cast<int>(proto::MessageType::link_notice) == 0xF00D);
  CHECK(proto::channel_of(proto::MessageType::link_notice) == proto::kChannelSession);
  CHECK_FALSE(proto::allowed_on_channel(proto::MessageType::link_notice, proto::kChannelPlayerSamples));
  CHECK(proto::may_send(proto::Sender::server, proto::MessageType::link_notice));
  CHECK_FALSE(proto::may_send(proto::Sender::client, proto::MessageType::link_notice));

  const proto::Message m = proto::LinkNotice{proto::LinkNoticeKind::server_protocol, 109, 0};
  const auto bytes = proto::encode(m);
  REQUIRE(bytes.has_value());
  // u16 type 253, u16 length 9, u8 kind 3, u32 109, u32 0 (little-endian).
  const std::vector<std::uint8_t> expected{0x0D, 0xF0, 0x09, 0x00, 0x03, 0x6D, 0x00, 0x00, 0x00, 0, 0, 0, 0};
  CHECK(*bytes == expected);

  proto::Message back;
  REQUIRE(proto::decode(*bytes, back) == proto::Error::none);
  CHECK(back == m);
  CHECK(proto::check_received(back, proto::Sender::server, proto::kChannelSession) == proto::Error::none);
  CHECK(proto::check_received(back, proto::Sender::client, proto::kChannelSession) != proto::Error::none);

  std::vector<std::uint8_t> other = expected;
  other[4] = 200;  // a kind of a later release
  REQUIRE(proto::decode(other, back) == proto::Error::none);
  REQUIRE(std::holds_alternative<proto::LinkNotice>(back));
  CHECK(static_cast<int>(std::get<proto::LinkNotice>(back).kind) == 200);

  std::vector<std::uint8_t> short_body = expected;
  short_body.pop_back();
  short_body[2] = 8;
  CHECK(proto::decode(short_body, back) != proto::Error::none);
}
