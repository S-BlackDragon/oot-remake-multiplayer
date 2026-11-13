// ORM cases moved out of upstream's tests/lichii/settings_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

// Tests for the emulator hook's settings parser (lichii/hook/coop_settings.cpp).
// That file has no emulator code, so it is built straight into coop_tests.
#include <doctest/doctest.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "coop_settings.h"

using CoopHook::ApplyEnvironment;
using CoopHook::ApplyIni;
using CoopHook::LoadSettings;
using CoopHook::Settings;

namespace {

using Env = std::map<std::string, std::string>;

void apply_env(const Env& env, Settings& s, std::vector<std::string>& errors) {
  ApplyEnvironment(
      [&env](const char* name) -> std::optional<std::string> {
        const auto it = env.find(name);
        if (it == env.end()) return std::nullopt;
        return it->second;
      },
      s, errors);
}

bool process_env_set(const char* name) {
#ifdef _MSC_VER
  char* value = nullptr;
  std::size_t size = 0;
  const bool set = _dupenv_s(&value, &size, name) == 0 && value != nullptr;
  std::free(value);
  return set;
#else
  return std::getenv(name) != nullptr;
#endif
}

bool any_coop_env() {
  return process_env_set("LICHII_COOP_SERVER") || process_env_set("LICHII_COOP_PORT") ||
         process_env_set("LICHII_COOP_NAME");
}

std::filesystem::path write_temp(const std::string& name, const std::string& text) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream(path, std::ios::binary) << text;
  return path;
}

}  // namespace

TEST_CASE("settings: transport key picks udp or ws") {
  Settings s;
  std::vector<std::string> errors;
  CHECK(s.transport == "udp");
  ApplyIni("server = host\ntransport = ws", s, errors);
  CHECK(errors.empty());
  CHECK(s.transport == "ws");
  ApplyIni("transport = udp", s, errors);
  CHECK(errors.empty());
  CHECK(s.transport == "udp");
}

TEST_CASE("settings: path key") {
  Settings s;
  std::vector<std::string> errors;
  CHECK(s.path == "/");
  ApplyIni("server = host\npath = /some/path", s, errors);
  CHECK(errors.empty());
  CHECK(s.path == "/some/path");
}

TEST_CASE("settings: server accepts udp:// and ws:// URLs") {
  Settings s;
  std::vector<std::string> errors;
  ApplyIni("server = udp://game.example", s, errors);
  CHECK(errors.empty());
  CHECK(s.server == "game.example");
  CHECK(s.transport == "udp");
  CHECK(s.port == 27510);

  ApplyIni("server = udp://game.example:5000", s, errors);
  CHECK(errors.empty());
  CHECK(s.server == "game.example");
  CHECK(s.transport == "udp");
  CHECK(s.port == 5000);

  ApplyIni("server = ws://game.example", s, errors);
  CHECK(errors.empty());
  CHECK(s.server == "game.example");
  CHECK(s.transport == "ws");
  CHECK(s.port == 27511);
  CHECK(s.path == "/");

  ApplyIni("server = ws://game.example:5000/coop", s, errors);
  CHECK(errors.empty());
  CHECK(s.server == "game.example");
  CHECK(s.transport == "ws");
  CHECK(s.port == 5000);
  CHECK(s.path == "/coop");
}

TEST_CASE("settings: transport key accepts wss") {
  Settings s;
  std::vector<std::string> errors;
  ApplyIni("server = host\ntransport = wss", s, errors);
  CHECK(errors.empty());
  CHECK(s.transport == "wss");
}

TEST_CASE("settings: server accepts wss:// URLs, default port 443") {
  Settings s;
  std::vector<std::string> errors;
  ApplyIni("server = wss://game.example", s, errors);
  CHECK(errors.empty());
  CHECK(s.server == "game.example");
  CHECK(s.transport == "wss");
  CHECK(s.port == 443);
  CHECK(s.path == "/");

  ApplyIni("server = wss://game.example:8443/coop", s, errors);
  CHECK(errors.empty());
  CHECK(s.server == "game.example");
  CHECK(s.transport == "wss");
  CHECK(s.port == 8443);
  CHECK(s.path == "/coop");

  ApplyIni("server = wss://game.example/coop", s, errors);
  CHECK(errors.empty());
  CHECK(s.server == "game.example");
  CHECK(s.transport == "wss");
  CHECK(s.port == 443);
  CHECK(s.path == "/coop");
}

TEST_CASE("settings: LICHII_COOP_TRANSPORT and LICHII_COOP_PATH") {
  Settings s;
  std::vector<std::string> errors;
  apply_env({{"LICHII_COOP_SERVER", "ws://game.example:5000/old"},
             {"LICHII_COOP_TRANSPORT", "udp"},
             {"LICHII_COOP_PATH", "/new"}},
            s, errors);
  CHECK(errors.empty());
  CHECK(s.server == "game.example");
  CHECK(s.port == 5000);
  CHECK(s.transport == "udp");  // the variable wins over the URL's scheme
  CHECK(s.path == "/new");      // and over the URL's path

  Settings url;
  std::vector<std::string> url_errors;
  apply_env({{"LICHII_COOP_SERVER", "udp://other.example"}}, url, url_errors);
  CHECK(url_errors.empty());
  CHECK(url.server == "other.example");
  CHECK(url.transport == "udp");
  CHECK(url.port == 27510);

  Settings tls;
  std::vector<std::string> tls_errors;
  apply_env({{"LICHII_COOP_SERVER", "wss://secure.example/coop"}}, tls, tls_errors);
  CHECK(tls_errors.empty());
  CHECK(tls.server == "secure.example");
  CHECK(tls.transport == "wss");
  CHECK(tls.port == 443);
  CHECK(tls.path == "/coop");
}

TEST_CASE("settings: a bad transport or path is an error") {
  Settings s;
  std::vector<std::string> errors;
  ApplyIni("transport = tcp", s, errors);
  REQUIRE(errors.size() == 1);
  CHECK(errors[0].find("transport must be udp, ws or wss") != std::string::npos);
  CHECK(s.transport == "udp");  // a bad value leaves the old one

  errors.clear();
  ApplyIni("path = no-slash", s, errors);
  REQUIRE(errors.size() == 1);
  CHECK(errors[0].find("path must start with '/'") != std::string::npos);
  CHECK(s.path == "/");
}

TEST_CASE("settings: color is #RRGGBB, in either case, from the ini or LICHII_COOP_COLOR (ORM)") {
  Settings s;
  CHECK_FALSE(s.color);  // none by default: the others use our number's colour
  std::vector<std::string> errors;
  ApplyIni("server = 10.0.0.1\ncolor = #2eB83C  # green\n", s, errors);
  CHECK(errors.empty());
  REQUIRE(s.color);
  CHECK(*s.color == 0x2EB83Cu);
  apply_env({{"LICHII_COOP_COLOR", " #FF0080 "}}, s, errors);
  CHECK(errors.empty());
  CHECK(*s.color == 0xFF0080u);
  // Since test10 the key is upstream's (CoopColors::Parse: a colour name or #RRGGBB, a u32);
  // what the ORM launcher writes (#RRGGBB) keeps working.
}

TEST_CASE("settings: a bad color is an error with its line, and co-op stays off (ORM)") {
  Settings s;
  std::vector<std::string> errors;
  ApplyIni("server = 10.0.0.1\ncolor = verdoso\n", s, errors);  // not a colour name CoopColors knows
  REQUIRE(errors.size() == 1);
  CHECK(errors[0].find("line 2") != std::string::npos);
  CHECK(errors[0].find("#RRGGBB") != std::string::npos);
  CHECK_FALSE(s.color);
  const auto path = write_temp("coop_settings_test_color.ini", "server = 10.0.0.1\ncolor = #12345\n");
  std::vector<std::string> load_errors;
  if (!any_coop_env() && !process_env_set("LICHII_COOP_COLOR")) {
    const Settings loaded = LoadSettings(path, load_errors);
    CHECK_FALSE(loaded.enabled());
    CHECK(load_errors.size() == 1);
  }
  std::filesystem::remove(path);
}
