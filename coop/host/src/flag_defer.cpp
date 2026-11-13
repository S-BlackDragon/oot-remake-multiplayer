// ORM: talk flags held back while their townsperson is in view, then written. The rules are
//   described in coop/host/flag_defer.hpp.
// Doc: oot-remake docs/BANDERAS_CHARLA.md (sections 7 and 10, D-R12).
// Switch: LICHII_COOP_TALK=shared_talk.txt (off in the kit), or LICHII_COOP_FLAGS_DEFER=1 for the
//   rules alone.
// Test: tests/host/flag_defer_test.cpp.

#include "coop/host/flag_defer.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>

namespace coop::host {
namespace {

using protocol::FlagEntry;

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::vector<std::string_view> tokens(std::string_view line) {
  std::vector<std::string_view> out;
  std::size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && is_space(line[i])) ++i;
    const std::size_t start = i;
    while (i < line.size() && !is_space(line[i])) ++i;
    if (i > start) out.push_back(line.substr(start, i - start));
  }
  return out;
}

bool is_hex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool is_name_char(char c) { return c > ' ' && c < 0x7f; }

// A flag token: 0x and 1 to 8 hex digits, or a name to hash (as SharedFlags).
std::optional<FlagKey> parse_flag(std::string_view token) {
  if (token.size() >= 3 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X') &&
      std::all_of(token.begin() + 2, token.end(), is_hex)) {
    const std::string_view digits = token.substr(2);
    if (digits.size() > 8) return std::nullopt;
    std::uint32_t hash = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), hash, 16);
    if (ec != std::errc() || end != digits.data() + digits.size()) return std::nullopt;
    return FlagKey{hash, 0};
  }
  if (token.empty() || !std::all_of(token.begin(), token.end(), is_name_char)) return std::nullopt;
  return FlagKey{flag_hash(token), 0};
}

std::optional<std::uint64_t> parse_seconds(std::string_view text) {
  if (text.empty() || text.size() > 7) return std::nullopt;
  std::uint64_t value = 0;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || end != text.data() + text.size()) return std::nullopt;
  return value * 1'000'000;
}

std::optional<float> parse_metres(std::string_view text) {
  if (text.empty() || text.size() > 8) return std::nullopt;
  unsigned whole = 0;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), whole);
  if (ec != std::errc() || end != text.data() + text.size() || whole > 10000) return std::nullopt;
  return static_cast<float>(whole);
}

bool starts_with_any(std::string_view name, const std::vector<std::string>& prefixes) {
  return std::any_of(prefixes.begin(), prefixes.end(),
                     [&](const std::string& p) { return name.starts_with(p); });
}

bool same_value(const FlagEntry& a, const FlagEntry& b) {
  return a.kind == b.kind && a.value == b.value;
}

// Never held, whatever the rules say: a 64-bit key (chests, one-time
// pickups), a count or a count delta (keys, tokens, heart pieces).
bool always_now(FlagStore& game, const FlagEntry& entry) {
  return entry.key.kind != FlagKeyKind::named || entry.kind == protocol::FlagKind::count_delta ||
         game.is_count(entry.key);
}

// The talk flags of Kokiri Forest, the Sacred Forest Meadow and two Malon
// placements, from the RomFS (ORM oot-remake/docs/BANDERAS_CHARLA.md): each
// one a saved Bool or Int (ResetTypeValue 8, SaveFileIndex 0) set by an NPC's
// event flow or read by its AI, or the live Presence flag of a placement
// (IsCheckLazy false). `actor` is a name prefix (nullptr: no actor, the
// flag only adds or takes away placements), `far` metres (0: off), `after`
// seconds (0: no time limit).
struct DefaultRule {
  const char* flag;
  const char* actor;
  const char* scene;
  int far;
  int after;
};

constexpr DefaultRule kDefaults[] = {
    // Placements that appear or go with the flag (Presence, not lazy).
    {"IsMalonAppear_HyruleCastleSecondTime", nullptr, "HyruleCastle", 0, 0},  // 0xfd1b310e
    {"IsMalonAppear_Market", nullptr, "Market", 0, 0},                        // 0x291f87b2
    {"HavePlayedEvent.Dm_Kokiri_0070", nullptr, "KokiriForest", 0, 0},        // 0x97137664, fog
    {"HavePlayedEvent.Dm_Kokiri_0140", nullptr, "KokiriForest", 0, 0},  // 0xebaec07f, leaves
    // Blockers: the Kokiri's blocker AI runs while the flag is set
    // (BlockerParam GameDataToEnableAI), and an air wall stands with it.
    {"KokiriForest_Npc_Kokiri_Blocker", "Npc_Kokiri_", "KokiriForest", 25, 120},  // 0xc6724c2e
    {"KokiriForest_Npc_Kokiri_Mido_Child_Blocker", "Npc_Kokiri_Mido_Child", "KokiriForest", 25,
     120},  // 0x502f6cea
    {"KokiriForest_Npc_Kokiri_007_Child_Blocker_00", "Npc_Kokiri_007_Child", "KokiriForest", 25,
     120},  // 0xe5dc5674
    // Where a townsperson stands or walks.
    {"KokiriForest_Kokiri_011_StopInFrontOfShop_00", "Npc_Kokiri_011_Child", "KokiriForest", 0,
     120},  // 0xfadf413f
    {"KokiriForest_LinkHouseGoDown", "Npc_Kokiri_Saria_Child", "KokiriForest", 0, 120},  // 0x1afd0ad0
    {"Npc_Kokiri_Saria_Child_WarpDone", "Npc_Kokiri_Saria_Child", "KokiriForest", 0, 120},  // 0xd852fccd
    {"Npc_Kokiri_Saria_Child_WarpStandby", "Npc_Kokiri_Saria_Child", "KokiriForest", 0,
     120},  // 0x27c52681
    {"KokiriForest_Npc_Kokiri_008_Child_LiftRock_AllBroken", "Npc_Kokiri_008_Child", "KokiriForest",
     0, 120},  // 0x6090cf95
    // Talk steps and "talked once" flags: what the townsperson says next,
    // and in some flows how it stands.
    {"HaveTalked_Npc_Kokiri_001_Shop_Child_PresentDekuShield", "Npc_Kokiri_001_Shop_Child",
     "KokiriForest", 0, 120},  // 0xbfc69399
    {"KokiriForest_Meet_DekuTree_BoughtDekuShield", "Npc_Kokiri_001_Shop_Child", "KokiriForest", 0,
     120},  // 0x1ec2cf04
    {"Npc_Kokiri_001_Shop_Child_Navi_HasTalked", "Npc_Kokiri_001_Shop_Child", "KokiriForest", 0,
     120},  // 0x9501cf99
    {"Npc_Kokiri_001_Shop_Child_Talk_Arrival", "Npc_Kokiri_001_Shop_Child", "KokiriForest", 0,
     120},  // 0x5fd8490e
    {"HaveTalked_Special.Npc_Kokiri_002_Child_DekuTreeTalk_Event27", "Npc_Kokiri_002_Child", "KokiriForest", 0, 120},  // 0x6506b01e
    {"HaveTalked_Special.Npc_Kokiri_002_Child_Talk_Event26", "Npc_Kokiri_002_Child", "KokiriForest", 0, 120},  // 0x36d488d0
    {"HaveTalked_Special.Npc_Kokiri_003_Child_Get_StoneOfForest_IsCompleted_Talk_Event68", "Npc_Kokiri_003_Child", "KokiriForest", 0, 120},  // 0x39196d32
    {"HaveTalked_Special.Npc_Kokiri_003_Child_Greetings_Event48", "Npc_Kokiri_003_Child", "KokiriForest", 0, 120},  // 0xcbd4a789
    {"HaveTalked_Special.Npc_Kokiri_003_Child_Talk_Event21", "Npc_Kokiri_003_Child", "KokiriForest", 0, 120},  // 0x37d8c29e
    {"KokiriForest_IsAfterGoronCityDarniaroomOpen_Npc_Kokiri_003_Child", "Npc_Kokiri_003_Child", "KokiriForest", 0, 120},  // 0x4c657880
    {"HaveTalked_Special.Npc_Kokiri_004_Child_DekuTreeTalk_Event23", "Npc_Kokiri_004_Child", "KokiriForest", 0, 120},  // 0x0e05cff3
    {"HaveTalked_Special.Npc_Kokiri_004_Child_Get_StoneOfForest_IsCompleted_Exp_Talk_Event45", "Npc_Kokiri_004_Child", "KokiriForest", 0, 120},  // 0x25eddcaa
    {"KokiriForest_Npc_Kokiri_004_Child_IsAfterFirstTalk", "Npc_Kokiri_004_Child", "KokiriForest", 0, 120},  // 0x47823545
    {"HaveTalked_Special.Npc_Kokiri_005_Child_GameStarted_Talk_Event42", "Npc_Kokiri_005_Child", "KokiriForest", 0, 120},  // 0x1c13069f
    {"HaveTalked_Special.Npc_Kokiri_005_Child_GameStarted_Talk_Event54", "Npc_Kokiri_005_Child", "KokiriForest", 0, 120},  // 0xf8dcc49d
    {"HaveTalked_Special.Npc_Kokiri_005_Child_Get_StoneOfForest_IsCompleted_Talk_Event12", "Npc_Kokiri_005_Child", "KokiriForest", 0, 120},  // 0x8ad4b333
    {"HaveTalked_Special.Npc_Kokiri_005_Child_Goto_HyruleCastle_IsCompleted_Talk_Event3", "Npc_Kokiri_005_Child", "KokiriForest", 0, 120},  // 0x327dd434
    {"HaveTalked_Special.Npc_Kokiri_005_Child_Talk_Event35", "Npc_Kokiri_005_Child", "KokiriForest", 0, 120},  // 0x19dadb95
    {"Npc_Kokiri_Noticed_PulledOut", "Npc_Kokiri_005_Child", "KokiriForest", 0, 120},  // 0x93e3da07
    {"HaveTalked_Special.Npc_Kokiri_006_Child_ActionTutorial_1st_Talk_Event269", "Npc_Kokiri_006_Child", "KokiriForest", 0, 120},  // 0x1001eb8d
    {"HaveTalked_Special.Npc_Kokiri_006_Child_Get_StoneOfForest_IsCompleted_Exp_Talk_Event106", "Npc_Kokiri_006_Child", "KokiriForest", 0, 120},  // 0x3473990e
    {"HaveTalked_Special.Npc_Kokiri_006_Child_Talk_Event21", "Npc_Kokiri_006_Child", "KokiriForest", 0, 120},  // 0xcb260fe8
    {"HaveTalked_Special.Npc_Kokiri_006_Child_Talk_Event68", "Npc_Kokiri_006_Child", "KokiriForest", 0, 120},  // 0x58bc20c6
    {"KokiriForest_Npc_Kokiri_006_Child_AttentionAction", "Npc_Kokiri_006_Child", "KokiriForest", 0, 120},  // 0xa6e896e2
    {"KokiriForest_Npc_Kokiri_006_Child_OperatedBackStep_IsTalked", "Npc_Kokiri_006_Child", "KokiriForest", 0, 120},  // 0x41d4bdad
    {"KokiriForest_Npc_Kokiri_006_Child_Talk_02", "Npc_Kokiri_006_Child", "KokiriForest", 0, 120},  // 0x613a8003
    {"KokiriForest_Npc_Kokiri_006_Child_Talk_04", "Npc_Kokiri_006_Child", "KokiriForest", 0, 120},  // 0x0817c135
    {"Npc_Kokiri_006_Child_FirstTalk", "Npc_Kokiri_006_Child", "KokiriForest", 0, 120},  // 0x5a339e31
    {"HaveTalked_Special.Npc_Kokiri_007_Child_GoToMido_Event16", "Npc_Kokiri_007_Child", "KokiriForest", 0, 120},  // 0x033d1e68
    {"HaveTalked_Special.Npc_Kokiri_007_Child_KokiriForest_Npc_Kokiri_007_Child_BlockEnd_Talk_Event9", "Npc_Kokiri_007_Child", "KokiriForest", 0, 120},  // 0xee540311
    {"Npc_Kokiri_007_Child_OcarinaFirst", "Npc_Kokiri_007_Child", "KokiriForest", 0, 120},  // 0x83fd30dc
    {"HaveTalked_Special.Npc_Kokiri_008_Child_GameStarted_ChoiceTalk_Event87", "Npc_Kokiri_008_Child", "KokiriForest", 0, 120},  // 0x355a7ab0
    {"HaveTalked_Special.Npc_Kokiri_008_Child_GameStarted_ChoiceTalk_Event89", "Npc_Kokiri_008_Child", "KokiriForest", 0, 120},  // 0xfe1c21e9
    {"HaveTalked_Special.Npc_Kokiri_008_Child_GameStarted_ChoiceTalk_First_Event79", "Npc_Kokiri_008_Child", "KokiriForest", 0, 120},  // 0x8a4a7c80
    {"HaveTalked_Special.Npc_Kokiri_008_Child_GameStarted_Talk_Event44", "Npc_Kokiri_008_Child", "KokiriForest", 0, 120},  // 0x841c3d99
    {"HaveTalked_Special.Npc_Kokiri_008_Child_Get_StoneOfForest_IsCompleted_Talk_Event22", "Npc_Kokiri_008_Child", "KokiriForest", 0, 120},  // 0x0b325e64
    {"HaveTalked_Special.Npc_Kokiri_008_Child_Goto_HyruleCastle_IsCompleted_Talk_Event5", "Npc_Kokiri_008_Child", "KokiriForest", 0, 120},  // 0x7e2f7c5e
    {"HaveTalked_Special.Npc_Kokiri_008_Child_LiftRock_AllBroken_Talk_Event166", "Npc_Kokiri_008_Child", "KokiriForest", 0, 120},  // 0xbf32253f
    {"KokiriForest_AfterShieldTalk_Npc_Kokiri_008_Child", "Npc_Kokiri_008_Child", "KokiriForest", 0, 120},  // 0xbb6ebebb
    {"KokiriForest_Npc_Kokiri_008_Child_AlreadyPaid", "Npc_Kokiri_008_Child", "KokiriForest", 0, 120},  // 0x4097ed9c
    {"KokiriForest_Npc_Kokiri_008_Child_TalkedInFront", "Npc_Kokiri_008_Child", "KokiriForest", 0, 120},  // 0x60e9c6e3
    {"HaveTalked_Special.Npc_Kokiri_009_Child_BeforeDekuClear_Event35", "Npc_Kokiri_009_Child", "KokiriForest", 0, 120},  // 0xf1cfffc4
    {"HaveTalked_Special.Npc_Kokiri_009_Child_BeforeDekuClear_Event89", "Npc_Kokiri_009_Child", "KokiriForest", 0, 120},  // 0x6c650fde
    {"HaveTalked_Special.Npc_Kokiri_009_Child_GetZeldaLullaby_Event72", "Npc_Kokiri_009_Child", "KokiriForest", 0, 120},  // 0xbb8ae460
    {"HaveTalked_Special.Npc_Kokiri_009_Child_Talk_Event17", "Npc_Kokiri_009_Child", "KokiriForest", 0, 120},  // 0x175d504c
    {"HaveTalked_Special.Npc_Kokiri_009_Child_Talk_Event23", "Npc_Kokiri_009_Child", "KokiriForest", 0, 120},  // 0xd5c0eafd
    {"HaveTalked_Special.Npc_Kokiri_009_Child_Talk_Event3", "Npc_Kokiri_009_Child", "KokiriForest", 0, 120},  // 0x2fc43bc8
    {"HaveTalked_Special.Npc_Kokiri_010_Child_FocusOnAttentionRock_ShieldDeku_Event94", "Npc_Kokiri_010_Child", "KokiriForest", 0, 120},  // 0x573344c1
    {"HaveTalked_Special.Npc_Kokiri_010_Child_Shield_Hylian_ReactionTalk_Event65", "Npc_Kokiri_010_Child", "KokiriForest", 0, 120},  // 0x3253ac59
    {"HaveTalked_Special.Npc_Kokiri_010_Child_Talk_Event14", "Npc_Kokiri_010_Child", "KokiriForest", 0, 120},  // 0x9990727f
    {"HaveTalked_Special.Npc_Kokiri_010_Child_Talk_Event18", "Npc_Kokiri_010_Child", "KokiriForest", 0, 120},  // 0x966363e7
    {"HaveTalked_Special.Npc_Kokiri_010_Child_Talk_Event23", "Npc_Kokiri_010_Child", "KokiriForest", 0, 120},  // 0x1b3b110c
    {"HaveTalked_Special.Npc_Kokiri_011_Child_BefoerStopInFrontOfShop_Talk_Event111", "Npc_Kokiri_011_Child", "KokiriForest", 0, 120},  // 0xcdc2a7c8
    {"HaveTalked_Special.Npc_Kokiri_011_Child_Get_StoneOfForest_IsCompleted_Exp_Talk_Event21", "Npc_Kokiri_011_Child", "KokiriForest", 0, 120},  // 0x22ea3ebf
    {"HaveTalked_Special.Npc_Kokiri_011_Child_Talk_Event1", "Npc_Kokiri_011_Child", "KokiriForest", 0, 120},  // 0x77cfc977
    {"HaveTalked_Special.Npc_Kokiri_011_Child_Talk_Event44", "Npc_Kokiri_011_Child", "KokiriForest", 0, 120},  // 0x49867d4a
    {"HaveTalked_Special.Npc_Kokiri_011_Child_Talk_Event57", "Npc_Kokiri_011_Child", "KokiriForest", 0, 120},  // 0x8474fe3d
    {"KokiriForest_Npc_Kokiri_011_Child_Talk_LinkPosition_Left", "Npc_Kokiri_011_Child", "KokiriForest", 0, 120},  // 0x804893a5
    {"KokiriForest_Npc_Kokiri_011_Child_Talk_LinkPosition_Right", "Npc_Kokiri_011_Child", "KokiriForest", 0, 120},  // 0x026cf794
    {"KokiriForest_Npc_Kokiri_011_Child_Talk_LinkPosition_Under", "Npc_Kokiri_011_Child", "KokiriForest", 0, 120},  // 0x0edf90c2
    {"Npc_Kokiri_011_FocusEvent_NearLeftWall", "Npc_Kokiri_011_Child", "KokiriForest", 0, 120},  // 0x5c347858
    {"HaveTalked_Special.Npc_Kokiri_012_Child_Get_StoneOfForest_IsCompleted_Talk_Event20", "Npc_Kokiri_012_Child", "KokiriForest", 0, 120},  // 0xf86555da
    {"HaveTalked_Special.Npc_Kokiri_012_Child_Talk_Event11", "Npc_Kokiri_012_Child", "KokiriForest", 0, 120},  // 0xaab6c2bd
    {"HaveTalked_Special.Npc_Kokiri_013_Child_Get_StoneOfForest_IsCompleted_Exp_Talk_Event70", "Npc_Kokiri_013_Child", "KokiriForest", 0, 120},  // 0x73f50530
    {"HaveTalked_Special.Npc_Kokiri_013_Child_Meet_DekuTree_Step01_Talk_Event2", "Npc_Kokiri_013_Child", "KokiriForest", 0, 120},  // 0x6bc5c56d
    {"HaveTalked_Special.Npc_Kokiri_013_Child_Meet_DekuTree_Step01_Talk_Event43", "Npc_Kokiri_013_Child", "KokiriForest", 0, 120},  // 0xcf8246c8
    {"HaveTalked_Special.Npc_Kokiri_Fado_Child_DekuTreeTalk_After_Event50", "Npc_Kokiri_Fado_Child", "KokiriForest", 0, 120},  // 0xfcbc28a9
    {"HaveTalked_Special.Npc_Kokiri_Fado_Child_Talk_Event15", "Npc_Kokiri_Fado_Child", "KokiriForest", 0, 120},  // 0xc23119ab
    {"HaveTalked_Special.Npc_Kokiri_Fado_Child_Talk_Event20", "Npc_Kokiri_Fado_Child", "KokiriForest", 0, 120},  // 0x410cea72
    {"Npc_Kokiri_Fado_Child_CameraAtDekuTree", "Npc_Kokiri_Fado_Child", "KokiriForest", 0, 120},  // 0x28e0a732
    {"Get_StoneOfForest_Complete_Mido_TalkEnd", "Npc_Kokiri_Mido_Child", "KokiriForest", 0, 120},  // 0x356bd37b
    {"HaveTalked_Special.Npc_Kokiri_Mido_Child_Get_StoneOfForest_Complete_Mido_TalkEnd_Event73", "Npc_Kokiri_Mido_Child", "KokiriForest", 0, 120},  // 0x70c89450
    {"HaveTalked_Special.Npc_Kokiri_Mido_Child_Get_StoneOfForest_Complete_Mido_TalkEnd_Event81", "Npc_Kokiri_Mido_Child", "KokiriForest", 0, 120},  // 0x7aeb091f
    {"HaveTalked_Special.Npc_Kokiri_Mido_Child_Meet_DekuTree_IsAfter_Step02_Exp_Talk_Event129", "Npc_Kokiri_Mido_Child", "KokiriForest", 0, 120},  // 0x11309a46
    {"HaveTalked_Special.Npc_Kokiri_Mido_Child_Talk_Event30", "Npc_Kokiri_Mido_Child", "KokiriForest", 0, 120},  // 0xc5577415
    {"KokiriForest_Npc_Kokiri_Mido_Child_Talk_01", "Npc_Kokiri_Mido_Child", "KokiriForest", 0, 120},  // 0xf3eda168
    {"KokiriForest_Npc_Kokiri_Mido_Child_Talk_02", "Npc_Kokiri_Mido_Child", "KokiriForest", 0, 120},  // 0x7a754164
    {"HaveTalked_Special.Npc_Kokiri_Saria_Child_Meet_DekuTree_IsAfter_Step01_Exp_Talk_Event78", "Npc_Kokiri_Saria_Child", "KokiriForest", 0, 120},  // 0xca2841c0
    {"HaveTalked_Special.Npc_Kokiri_Saria_Child_Talk_Event20", "Npc_Kokiri_Saria_Child", "KokiriForest", 0, 120},  // 0xa6ba5eee
    {"HaveTalked_Special.Npc_Kokiri_Saria_Child_Talk_Event37", "Npc_Kokiri_Saria_Child", "KokiriForest", 0, 120},  // 0x295d3490
    {"HaveTalked_Special.Npc_Kokiri_Saria_Child_Talk_Event40", "Npc_Kokiri_Saria_Child", "KokiriForest", 0, 120},  // 0x2ce0fe8b
    {"HaveTalked_Special.Npc_Kokiri_Saria_Child_Talk_Event55", "Npc_Kokiri_Saria_Child", "KokiriForest", 0, 120},  // 0x050148f8
    {"HaveTalked_Special.Npc_Kokiri_Saria_Child_Talk_Event67", "Npc_Kokiri_Saria_Child", "KokiriForest", 0, 120},  // 0x2d0e0145
    {"KokiriForest_Npc_Kokiri_Saria_Child_Talk_01", "Npc_Kokiri_Saria_Child", "KokiriForest", 0, 120},  // 0x092699eb
    {"KokiriForest_Npc_Kokiri_Saria_Child_Talk_02", "Npc_Kokiri_Saria_Child", "KokiriForest", 0, 120},  // 0x6fda3c3d
    {"HaveTalked_Special.Npc_Kokiri_Saria_Child_SacredForestMeadow_PlayOcarina_InFront_Event45", "Npc_Kokiri_Saria_Child_SacredForestMeadow", "SacredForestMeadow", 0, 120},  // 0x56241bd7
    {"HaveTalked_Special.Npc_Kokiri_Saria_Child_SacredForestMeadow_Talk_Event4", "Npc_Kokiri_Saria_Child_SacredForestMeadow", "SacredForestMeadow", 0, 120},  // 0x03527af3
    {"HaveTalked_Special.Npc_Kokiri_Saria_Child_SacredForestMeadow_Talk_Event66", "Npc_Kokiri_Saria_Child_SacredForestMeadow", "SacredForestMeadow", 0, 120},  // 0x5542c509
    {"Npc_Kokiri_Saria_Child_SacredForestMeadow_PlayedSariaSong", "Npc_Kokiri_Saria_Child_SacredForestMeadow", "SacredForestMeadow", 0, 120},  // 0xd40c23b9
};

}  // namespace

std::string_view to_string(DeferWhy why) {
  switch (why) {
    case DeferWhy::held: return "held";
    case DeferWhy::scene_unknown: return "no scene";
    case DeferWhy::scene_changed: return "scene changed";
    case DeferWhy::other_scene: return "another scene";
    case DeferWhy::actors_gone: return "its actors are not loaded";
    case DeferWhy::actors_copied: return "its actors are copies another game moves";
    case DeferWhy::far: return "its actors are far away";
    case DeferWhy::timeout: return "time ran out";
    case DeferWhy::game_changed: return "the game changed it";
  }
  return "?";
}

bool DeferRules::set(FlagKey key, DeferRule rule) {
  if (key.kind != FlagKeyKind::named) return false;
  if (!rules_.contains(key) && rules_.size() >= kMaxRules) return false;
  rules_[key] = std::move(rule);
  return true;
}

const DeferRule* DeferRules::find(FlagKey key) const {
  const auto it = rules_.find(key);
  if (it == rules_.end() || it->second.now) return nullptr;
  return &it->second;
}

void DeferRules::merge(const DeferRules& over) {
  for (const auto& [key, rule] : over.rules_) {
    if (set(key, rule)) {
      if (const auto it = over.names_.find(key); it != over.names_.end()) names_[key] = it->second;
    }
  }
}

std::vector<FlagKey> DeferRules::keys() const {
  std::vector<FlagKey> out;
  out.reserve(rules_.size());
  for (const auto& [key, rule] : rules_) out.push_back(key);
  return out;
}

std::string_view DeferRules::name(FlagKey key) const {
  const auto it = names_.find(key);
  return it == names_.end() ? std::string_view{} : std::string_view(it->second);
}

std::optional<DeferRules> DeferRules::parse(std::string_view text, std::string* error) {
  DeferRules out;
  std::size_t line_number = 0;
  auto fail = [&](std::string_view why) -> std::optional<DeferRules> {
    if (error) *error = "line " + std::to_string(line_number) + ": " + std::string(why);
    return std::nullopt;
  };
  while (!text.empty()) {
    ++line_number;
    const std::size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) {
      line = line.substr(0, hash);
    }
    const auto words = tokens(line);
    if (words.empty()) continue;
    const auto key = parse_flag(words[0]);
    if (!key) return fail("not a flag name or 0x hash: " + std::string(words[0]));
    DeferRule rule;
    bool far_seen = false, after_seen = false;
    for (std::size_t i = 1; i < words.size(); ++i) {
      const std::string_view w = words[i];
      if (w == "now") {
        if (words.size() != 2) return fail("\"now\" goes alone");
        rule.now = true;
      } else if (w.starts_with("scene:") && w.size() > 6) {
        rule.scenes.emplace_back(w.substr(6));
      } else if (w.starts_with("actor:") && w.size() > 6) {
        rule.actors.emplace_back(w.substr(6));
      } else if (w.starts_with("far:")) {
        const auto m = parse_metres(w.substr(4));
        if (!m || far_seen) return fail("bad or second far: " + std::string(w));
        rule.far_m = *m;
        far_seen = true;
      } else if (w.starts_with("after:")) {
        const auto s = parse_seconds(w.substr(6));
        if (!s || after_seen) return fail("bad or second after: " + std::string(w));
        rule.after_us = *s;
        after_seen = true;
      } else {
        return fail("unknown option: " + std::string(w));
      }
    }
    if (rule.far_m > 0.0f && rule.actors.empty()) return fail("far: needs an actor:");
    if (!out.set(*key, std::move(rule))) return fail("too many rules");
    if (words[0].size() < 2 || words[0][0] != '0' || (words[0][1] != 'x' && words[0][1] != 'X')) {
      out.names_[*key] = std::string(words[0]);
    } else {
      out.names_.erase(*key);
    }
  }
  return out;
}

std::optional<DeferRules> DeferRules::load(const std::filesystem::path& path, std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (error) *error = "cannot open the file";
    return std::nullopt;
  }
  std::ostringstream text;
  text << file.rdbuf();
  return parse(text.str(), error);
}

DeferRules default_defer_rules() {
  DeferRules out;
  for (const DefaultRule& d : kDefaults) {
    DeferRule rule;
    rule.scenes.emplace_back(d.scene);
    if (d.actor != nullptr) rule.actors.emplace_back(d.actor);
    rule.far_m = static_cast<float>(d.far);
    rule.after_us = static_cast<std::uint64_t>(d.after) * 1'000'000;
    const FlagKey key{flag_hash(d.flag), 0};
    out.set(key, std::move(rule));
    out.names_[key] = d.flag;
  }
  return out;
}

std::optional<DeferWhy> FlagDeferral::release(const DeferRule& rule, const DeferView& view,
                                              std::string_view scene, std::uint64_t since_us) {
  if (!view.scene || view.scene->empty()) return DeferWhy::scene_unknown;
  if (*view.scene != scene) return DeferWhy::scene_changed;
  if (!rule.scenes.empty() && !starts_with_any(*view.scene, rule.scenes)) {
    return DeferWhy::other_scene;
  }
  if (!rule.actors.empty() && view.actors) {
    bool any = false, all_copies = true, all_far = rule.far_m > 0.0f && view.link.has_value();
    for (const SeenActor& a : *view.actors) {
      if (!starts_with_any(a.name, rule.actors)) continue;
      any = true;
      all_copies = all_copies && a.copy;
      if (all_far) {
        if (!a.position) {
          all_far = false;
        } else {
          const float dx = (*a.position)[0] - (*view.link)[0];
          const float dy = (*a.position)[1] - (*view.link)[1];
          const float dz = (*a.position)[2] - (*view.link)[2];
          all_far = std::sqrt(dx * dx + dy * dy + dz * dz) > rule.far_m;
        }
      }
    }
    if (!any) return DeferWhy::actors_gone;
    if (all_copies) return DeferWhy::actors_copied;
    if (all_far) return DeferWhy::far;
  }
  if (rule.after_us != 0 && view.now_us >= since_us && view.now_us - since_us >= rule.after_us) {
    return DeferWhy::timeout;
  }
  return std::nullopt;
}

bool FlagDeferral::changed_by_game(FlagStore& game, FlagKey key, const Held& held,
                                   std::optional<FlagEntry>* now) {
  *now = game.read(key);
  if (!*now || !held.game_before || same_value(**now, *held.game_before)) return false;
  (*now)->key = key;
  ++stats_.dropped;
  notes_.push_back({**now, DeferWhy::game_changed});
  held_.erase(key);
  return true;
}

void FlagDeferral::tick(FlagStore& game, const DeferView& view) {
  for (auto it = held_.begin(); it != held_.end();) {
    const FlagKey key = it->first;
    const Held& held = it->second;
    ++it;  // the entry may go below
    std::optional<FlagEntry> now;
    if (changed_by_game(game, key, held, &now)) continue;
    const DeferRule* rule = rules_.find(key);
    const auto why = rule ? release(*rule, view, held.scene, held.since_us)
                          : std::optional<DeferWhy>(DeferWhy::timeout);
    if (!why) continue;
    if (!game.write(held.entry)) {
      ++stats_.write_failures;  // tried again next frame
      continue;
    }
    ++stats_.released;
    notes_.push_back({held.entry, *why});
    held_.erase(key);
  }
}

std::optional<FlagEntry> FlagDeferral::read(FlagStore& game, FlagKey key) {
  const auto it = held_.find(key);
  if (it == held_.end()) return game.read(key);
  std::optional<FlagEntry> now;
  if (changed_by_game(game, key, it->second, &now)) return now;
  return it->second.entry;
}

bool FlagDeferral::write(FlagStore& game, const FlagEntry& entry, const DeferView& view) {
  const FlagKey key = key_of(entry);
  const DeferRule* rule = always_now(game, entry) ? nullptr : rules_.find(key);
  auto it = held_.find(key);
  const bool was_held = it != held_.end();
  if (rule == nullptr) {
    if (it != held_.end()) held_.erase(it);  // a newer value: it goes as it is
    ++stats_.passed;
    return game.write(entry);
  }
  std::optional<FlagEntry> before = game.read(key);
  if (before) before->key = key;
  if (before && same_value(*before, entry)) {
    // The game has it already: nothing to hold (a held older value is moot).
    if (it != held_.end()) held_.erase(it);
    ++stats_.passed;
    return true;
  }
  // A value held for a while keeps its first time and scene, so a flag that
  // keeps changing still times out.
  const std::string scene = was_held ? it->second.scene : view.scene.value_or("");
  const std::uint64_t since = was_held ? it->second.since_us : view.now_us;
  if (const auto why = release(*rule, view, scene, since)) {
    if (was_held) held_.erase(it);
    ++stats_.passed;
    if (passed_.size() < 64) {
      DeferNote note{entry, *why};
      note.entry.key = key;
      passed_.push_back(note);
    }
    return game.write(entry);
  }
  Held& held = held_[key];
  held.entry = entry;
  held.entry.key = key;
  held.game_before = before;
  held.scene = scene;
  held.since_us = since;
  if (!was_held) {
    ++stats_.held;
    notes_.push_back({held.entry, DeferWhy::held});
  }
  return true;
}

bool FlagDeferral::write_exact(FlagStore& game, const FlagEntry& entry) {
  held_.erase(key_of(entry));
  return game.write_exact(entry);
}

std::vector<DeferNote> FlagDeferral::take_notes() {
  auto out = std::move(notes_);
  notes_.clear();
  return out;
}

std::vector<FlagChange> FlagChanges::read(FlagStore& game) {
  std::vector<FlagChange> out;
  for (const FlagKey key : keys_) {
    auto now = game.read(key);
    if (!now) continue;
    now->key = key;
    const auto it = last_.find(key);
    if (it == last_.end()) {
      last_.emplace(key, *now);
      continue;
    }
    if (same_value(it->second, *now)) continue;
    out.push_back({key, it->second, *now});
    it->second = *now;
  }
  return out;
}

}  // namespace coop::host
