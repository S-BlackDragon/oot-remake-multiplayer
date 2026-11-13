#pragma once

// The messages of protocol version 0, stages 0 and 1 (docs/protocol.md).
// Plain structs only: no sockets, no ENet, no game. The codec in codec.hpp
// turns them into bytes and back.

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "coop/protocol/join_password.hpp"
#include "coop/protocol/traffic_crypto.hpp"
#include "coop/protocol/orm_types.hpp"  // ORM: our numbers (oot-remake docs/TIPOS_MENSAJE.md)

// ORM: Qt's `slots` keyword macro (our Qt files, lichii/hook/coop_window.cpp, include this
// header) would eat upstream's ObjectComponent::slots and ObjectSample::slots.
#pragma push_macro("slots")
#undef slots

namespace coop::protocol {

// 2: OBJECT_SPAWN of a placed id; 3: TIME_OF_DAY; 4: the messages
// (types 28 to 39) and the server sending HANDOVER_ASK; 9: the join password
// (JOIN_CHALLENGE, JOIN_PROOF, REFUSED bad_password).
// ORM: ours is 100 + upstream's 10, so a stock build and this one refuse
// each other as a wrong protocol version.
inline constexpr std::uint16_t kProtocolVersion = 110;
inline constexpr std::uint16_t kDefaultPort = 27510;
inline constexpr std::uint8_t kMaxPlayers = 6;

// Size of the message header: u16 type, u16 length.
inline constexpr std::size_t kHeaderSize = 4;
// Size of a component header: u16 id, u16 length.
inline constexpr std::size_t kComponentHeaderSize = 4;

// Maximum lengths of string fields, in Unicode characters (code points).
// A string may also never be longer than 4 bytes per character, must be valid
// UTF-8 and holds no control characters (U+0000-U+001F, U+007F-U+009F).
inline constexpr std::size_t kMaxBuildIdChars = 64;
inline constexpr std::size_t kMaxGameBuildIdChars = 32;
inline constexpr std::size_t kMaxGameVersionChars = 16;
inline constexpr std::size_t kMaxNicknameChars = 16;
inline constexpr std::size_t kMaxChatChars = 200;
inline constexpr std::size_t kMaxSceneChars = 64;
inline constexpr std::size_t kMaxAnimationChars = 64;

// ENet channels (docs/protocol.md, "Transport").
inline constexpr std::uint8_t kChannelSession = 0;
inline constexpr std::uint8_t kChannelProgress = 1;
inline constexpr std::uint8_t kChannelOwnership = 2;
inline constexpr std::uint8_t kChannelPlayerSamples = 3;
inline constexpr std::uint8_t kChannelObjectSamples = 4;
inline constexpr std::uint8_t kChannelCount = 5;
// The largest UDP payload ENet puts in one datagram (its MTU), on the server
// and in every game; ENet takes the smaller of the two sides, so either side
// alone is enough. ENet's default of 1392 makes 1420-byte IP packets, more
// than a VPN adapter carries (Hamachi about 1404, Tailscale 1280): those were
// split or dropped, and a large reliable message (a snapshot) was sent again
// until the connection timed out (issue 143). A home line can be smaller
// still: a DS-Lite (IPv4 in IPv6) or PPPoE tunnel, one reporter's at 1200
// bytes, where 1200-byte datagrams (1228-byte packets) did not fit either.
// 1100 + 28 header bytes fits them all. Not part of the protocol version:
// ENet agrees on it at connect.
inline constexpr std::uint32_t kDatagramBytes = 1100;

// Message type numbers on the wire. Never renumber; add new ones at the end.
enum class MessageType : std::uint16_t {
  hello = 1,
  welcome = 2,
  refused = 3,
  player_joined = 4,
  player_left = 5,
  ping = 6,
  pong = 7,
  player_status = 8,
  chat = 9,
  player_sample = 10,
  test_payload = 11,
  join_partner = 12,
  flag_set = 13,
  flag_state = 14,
  flag_snapshot = 15,
  host = 16,
  player_equip = 17,
  object_present = 18,
  object_owner = 19,
  handover_ask = 20,
  handover_state = 21,
  object_spawn = 22,
  object_sample = 23,
  object_remove = 24,
  damage = 25,
  player_pose = 26,
  time_of_day = 27,
  grass_cut = 28,
  object_group = 29,  // 30, 31: kept for enemy ownership
  world_break = 32,
  world_move = 33,
  world_place = 34,
  world_hit = 35,
  drop_spawn = 36,
  drop_take = 37,
  drop_ask = 38,
  boss_room = 50,  // 50 to 54: boss rooms (docs/dungeons-boss.md)
  sign_cut = 80,   // 80 to 84: signs (docs/world.md, "Signs")
  flag_reset = 135,  // 135 to 139: the host's save load (docs/protocol.md, "Saves")
  ocarina_note = 140,  // 140 to 144: the other players' Ocarina (docs/protocol.md, "Ocarina")
  flag_origin = 150,  // 150 to 154: who made a shared change (docs/protocol.md, "Who did it")
  join_challenge = 191,  // 191 to 195: the join password (docs/protocol.md, "Join password")
  join_proof = 192,
  save_ask = 206,   // 206 to 215: the host's save sent to guests (docs/protocol.md, "Save transfer")
  save_part = 207,
  pvp_mode = 241,  // 241 to 250: player against player (docs/protocol.md, "PvP")
  pvp_hit = 242,
  pvp_hurt = 243,
  player_life = 251,  // 251 to 255: the co-op HUD (docs/protocol.md, "Hearts")
  fish_lure = 320,  // 320 to 324: the fishing pond (docs/protocol.md, "Fishing")
  rejoin_token = 345,  // 345 to 349: rejoin after a crash or drop (docs/protocol.md, "Rejoin")
  rejoin_claim = 346,
  dungeon_event = 355,  // 355 to 359: a room gimmick's event started (docs/protocol.md, "Gimmick events")
  player_ride = 385,  // 385 to 389: riding Epona (docs/protocol.md, "Riding")
  platform_clock = 400,  // 400 to 404: moving platforms' clocks (docs/protocol.md, "Moving platforms")
  platform_counter = 401,  // looping platforms' pause counters (docs/protocol.md, "Moving platforms")
  // ORM (oot-remake), 0xF000 to 0xF0FF, every number from orm_types.hpp (the one
  // table, oot-remake docs/TIPOS_MENSAJE.md). Were 60 to 65 up to protocol 108, 240 to 254 in 109.
  revive = orm::type::revive,              // ORM: a mate gets a downed player up (0xF001 kept)
  group_defeat = orm::type::group_defeat,  // ORM: every player is down: the game over for all
  story_event = orm::type::story_event,    // ORM: a story cutscene for everyone in the scene
  player_look = orm::type::player_look,    // ORM: a player's colour (name tag, later the tunic)
  object_pose = orm::type::object_pose,    // ORM: the bones of the townsperson a player talks to
  chest_open = orm::type::chest_open,      // ORM: a player's game started opening a shared chest (its lid, on every screen)
  orm_ocarina_note = orm::type::ocarina_note,  // ORM: a note or song a player plays, heard at its puppet (upstream's ocarina_note 140: notes only)
  group_settings = orm::type::group_settings,  // ORM: a player's settings that change the shared world (orm_messages.hpp)
  link_notice = orm::type::link_notice,    // ORM: the relay tells one player how his link keeps up
  host_choice = orm::type::host_choice,    // ORM: who hosts, by the relay's measures or by hand
  room_switch = orm::type::room_switch,    // ORM: a room-wide switch set live (orm_messages.hpp)
  player_hit = orm::type::player_hit,      // ORM: a weapon hit on a mate's puppet, to that mate
  player_call = orm::type::player_call,    // ORM: "come to me" to a mate, or the host's to all
  player_rename = orm::type::player_rename,  // ORM: a new name, checked by the server
};

// ORM: upstream's types stay inside its blocks and below ours (orm_types.hpp).
static_assert(orm::in_upstream_type_block(static_cast<std::uint16_t>(MessageType::platform_counter)) &&
                  static_cast<std::uint16_t>(MessageType::platform_counter) == orm::kUpstreamLastType &&
                  static_cast<std::uint16_t>(MessageType::platform_counter) < orm::kTypeBlock.first,
              "ORM: re-check orm::kUpstreamTypeBlocks against upstream's MessageType");

// PLAYER_SAMPLE component ids.
enum class SampleComponent : std::uint16_t {
  time = 1,
  position = 2,
  velocity = 3,
  rotation = 4,
  scene = 5,
  age = 6,
  animation = 7,
  // ORM (oot-remake docs/TEMPLOS.md): the moving floor the player stands on.
  // High, away from the ids upstream adds next; a build without it skips it
  // by its length.
  ride = orm::sample_component::ride,  // 30 (orm_types.hpp)
};
// ORM: a new upstream component here must stay below ours (orm_types.hpp).
static_assert(static_cast<std::uint16_t>(SampleComponent::animation) == orm::kUpstreamLastSampleComponent,
              "ORM: upstream's PLAYER_SAMPLE components changed: update orm_types.hpp");

// PLAYER_EQUIP component ids. 1 to 8 are the game's equipped actor names
// (PlayerStatus.Equip*), 9 the drawn bits, 10 the actor name of the item in
// the hand slot (slingshot, bow; read from the piece itself).
enum class EquipComponent : std::uint16_t {
  weapon = 1,
  shield = 2,
  armor = 3,
  boots = 4,
  head = 5,
  hand = 6,
  ocarina = 7,
  arrow = 8,
  drawn = 9,
  held = 10,
  // The actor in each other hand slot of the game's EquipmentUser, by name,
  // as `held` is for slot 3 (docs/puppet.md, "The other hand slots").
  weapon_slot = 11,   // slot 0, Weapon: the sword, or a Deku stick in its place
  sub_weapon = 12,    // slot 1, SubWeapon
  third_weapon = 13,  // slot 2, ThirdWeapon
  tool = 14,          // slot 5, Tool
  sub_tool = 15,      // slot 6, SubTool
  // The player's colour (coop-options.ini `color`): 3 bytes, red, green,
  // blue. Others tint that player's puppet with it (docs/puppet.md, "Colour").
  color = 16,
  // ORM (oot-remake docs/TIPOS_MENSAJE.md, docs/INVENTARIO.md "Cartera de cada
  // uno"): the player's own wallet, PlayerStatus.CurrentRupee, a u32 of 4
  // bytes, so every game can show everyone's rupees. Ours, from the top block
  // (24 to 31, orm_types.hpp; 30 is the mount). A build without it skips it
  // by its length.
  rupees = orm::equip_component::rupees,
  // ORM D-R19 (oot-remake docs/EPONA.md): the horse the player is on (getting
  // on or riding), by its actor's name (Animal_Epona_Adult, Animal_Horse_For_Ingo,
  // ...); "" on foot. Older readers skip it by its length. 30, in ORM's
  // block 24-31 (oot-remake docs/TIPOS_MENSAJE.md, orm_types.hpp
  // orm::equip_component::mount); 16 is upstream's next slot.
  mount = orm::equip_component::mount,
};
// ORM: a new upstream component here must stay below ours (orm_types.hpp).
static_assert(static_cast<std::uint16_t>(EquipComponent::color) == orm::kUpstreamLastEquipComponent,
              "ORM: upstream's PLAYER_EQUIP components changed: update orm_types.hpp");

// The most rupees a PLAYER_EQUIP may carry (a wallet of the game holds far
// fewer); a larger value is refused, a sender clamps to it.
inline constexpr std::uint32_t kMaxRupees = 99999;

// Longest equipped actor name: the game keeps each in a String64 flag.
inline constexpr std::size_t kMaxEquipNameChars = 64;

// Player numbers are 1 to kMaxPlayers. In PLAYER_STATUS, CHAT and PLAYER_SAMPLE a
// client sends 0 ("me"); the server fills in the sender's number when it
// relays the message to the others.
inline constexpr std::uint8_t kPlayerSelf = 0;

// HELLO, client to server. The protocol version is always the first two bytes
// of the body, in every protocol version, so a server can refuse a client
// whose HELLO it cannot otherwise read (see hello_protocol_version).
struct Hello {
  std::uint16_t protocol_version = kProtocolVersion;
  std::string build_id;       // emulator or recomp, and its version
  std::string game_build_id;  // for example "e0f3ccfaf50065ae"
  std::string game_version;   // for example "1.1.1"
  std::string nickname;       // 1 to 16 characters
  // The game's X25519 public key for traffic encryption (traffic_crypto.hpp),
  // 32 bytes after the nickname. Sent by every game since protocol 10; a
  // server with a join password refuses a HELLO without it.
  std::optional<KeyShare> key_share{};
  // Since protocol 10 (this release's): the player number this game had in its last session
  // (1 to kMaxPlayers), one byte after the key share (or after the nickname without
  // one). The server gives it back when it is free, so a server restart
  // does not swap the players' numbers (and with them their colours and
  // every object's owner). Nothing on a game's first join.
  std::optional<std::uint8_t> previous_number{};
  bool operator==(const Hello&) const = default;
};

struct PlayerInfo {
  std::uint8_t number = 1;  // 1 to kMaxPlayers
  std::string nickname;
  bool operator==(const PlayerInfo&) const = default;
};

struct Welcome {
  std::uint8_t player_number = 1;  // 1 to kMaxPlayers
  std::uint64_t server_time_us = 0;
  std::uint16_t tick_rate = 0;
  std::vector<PlayerInfo> players;  // the players already in, at most kMaxPlayers
  bool operator==(const Welcome&) const = default;
};

// Reasons are kept as raw numbers on decode, so a newer server's reason
// still reaches the client (which shows it as unknown).
enum class RefuseReason : std::uint8_t {
  wrong_protocol_version = 1,
  wrong_game_build = 2,
  server_full = 3,
  name_taken = 4,
  bad_name = 5,  // empty or nothing but spaces
  bad_password = 6,  // the server has a join password and the JOIN_PROOF did not match
  // Never sent: the game's own refusal of a server that did not encrypt
  // although the game has a join password (docs/protocol.md "Encrypted
  // traffic"), so nobody on the path can stand in for a no-password server.
  not_encrypted = 200,
};

struct Refused {
  RefuseReason reason = RefuseReason::wrong_protocol_version;
  bool operator==(const Refused&) const = default;
};

// JOIN_CHALLENGE, server to client, in answer to a HELLO when the server has
// a join password: a fresh random nonce for this connection only. The client
// answers with JOIN_PROOF; WELCOME or REFUSED follows.
struct JoinChallenge {
  JoinNonce nonce{};
  // The server's X25519 public key, 32 bytes after the nonce, when the
  // HELLO had one: from the JOIN_PROOF on, the connection is encrypted.
  std::optional<KeyShare> key_share{};
  bool operator==(const JoinChallenge&) const = default;
};

// JOIN_PROOF, client to server: join_proof(password, nonce) for the nonce of
// this connection's JOIN_CHALLENGE (an empty password when it has none).
struct JoinProof {
  JoinProofBytes proof{};
  bool operator==(const JoinProof&) const = default;
};

// REJOIN_TOKEN, server to client, right after WELCOME: a fresh random token
// for this player's place. Only a connection that shows it (REJOIN_CLAIM)
// may take that place over while the old connection is quiet, so two players
// behind one address cannot push each other out by name.
using RejoinTokenBytes = JoinNonce;
struct RejoinToken {
  RejoinTokenBytes token{};
  bool operator==(const RejoinToken&) const = default;
};

// REJOIN_CLAIM, client to server, before its HELLO on a new connection: the
// token its last WELCOME came with. Without one no takeover happens.
struct RejoinClaim {
  RejoinTokenBytes token{};
  bool operator==(const RejoinClaim&) const = default;
};

struct PlayerJoined {
  std::uint8_t player_number = 1;  // 1 to kMaxPlayers
  std::string nickname;
  bool operator==(const PlayerJoined&) const = default;
};

enum class LeaveReason : std::uint8_t {
  left = 0,
  timed_out = 1,
  kicked = 2,
};

struct PlayerLeft {
  std::uint8_t player_number = 1;  // 1 to kMaxPlayers
  LeaveReason reason = LeaveReason::left;  // raw value kept on decode
  bool operator==(const PlayerLeft&) const = default;
};

struct Ping {
  std::uint64_t sender_time_us = 0;
  bool operator==(const Ping&) const = default;
};

struct Pong {
  std::uint64_t ping_sender_time_us = 0;  // echoed from the PING
  std::uint64_t receiver_time_us = 0;     // the PONG sender's clock
  bool operator==(const Pong&) const = default;
};

// PLAYER_STATUS flag bits. Unknown bits are kept, not refused.
namespace status {
inline constexpr std::uint8_t paused = 1u << 0;
inline constexpr std::uint8_t loading = 1u << 1;
inline constexpr std::uint8_t in_cutscene = 1u << 2;
inline constexpr std::uint8_t in_menu = 1u << 3;
inline constexpr std::uint8_t dead = 1u << 4;
}  // namespace status
// ORM: our PLAYER_STATUS bits count down from 7 (orm_types.hpp).
static_assert(static_cast<unsigned>(status::dead) == (1u << orm::kUpstreamLastStatusBit),
              "ORM: upstream's PLAYER_STATUS bits changed: update orm_types.hpp");

struct PlayerStatus {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, 1 to kMaxPlayers relayed
  std::uint8_t flags = 0;
  bool operator==(const PlayerStatus&) const = default;
};

// PLAYER_LIFE (protocol 10): a player's hearts, in quarter hearts as the game
// keeps them (4 = one heart): life now and the most it can be. Sent reliably
// on every change and again when another player joins; the others draw it
// under their own hearts (docs/overlay.md). Same player field rules as
// PLAYER_STATUS. max_life 0: not known (the game's Link is not loaded).
struct PlayerLife {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, 1 to kMaxPlayers relayed
  std::uint16_t life = 0;
  std::uint16_t max_life = 0;
  bool operator==(const PlayerLife&) const = default;
};

// The most quarter hearts PLAYER_LIFE carries: 20 hearts, with room for a
// game that allows more.
inline constexpr std::uint16_t kMaxLifeQuarters = 4 * 40;

struct Chat {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, 1 to kMaxPlayers relayed
  std::string text;  // up to 200 characters
  bool operator==(const Chat&) const = default;
};

// The time of a sample is on the server clock, not the sender's: the server
// relays it unchanged, and a receiver can place it only on a clock everyone
// shares. The sender works it out from its own clock and its offset estimate
// (coop::net::Client::server_now_us or to_server_time).
struct SampleTime {
  std::uint64_t server_time_us = 0;
  std::uint32_t sequence = 0;
  bool operator==(const SampleTime&) const = default;
};

using Vec3 = std::array<float, 3>;
using Quat = std::array<float, 4>;  // x, y, z, w

enum class Age : std::uint8_t {
  child = 0,
  adult = 1,
};

struct Animation {
  std::string name;
  float frame = 0.0f;
  float rate = 0.0f;
  bool operator==(const Animation&) const = default;
};

// PLAYER_RIDE (protocol 10, issue 199): the horse a player rides, sent with
// its samples (channel 3) while it rides, and once with riding off when it
// gets down. The others draw a copy of the horse under the rider's puppet:
// at the puppet's place plus `offset` (the horse's place minus the rider's,
// metres), turned `rotation`, playing `animation` (the horse's AS slot 0).
// Same player field rules as PLAYER_STATUS. Floats must be finite.
inline constexpr std::uint8_t kRideRiding = 0x01;
inline constexpr float kMaxRideOffset = 10.0f;  // each axis
inline constexpr std::size_t kMaxRideHorseChars = 64;
struct PlayerRide {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, 1 to kMaxPlayers relayed
  std::uint8_t flags = 0;                     // kRideRiding; other bits refused
  std::string horse;                          // the horse's actor name; empty when off
  Vec3 offset{};
  Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};
  std::optional<Animation> animation;
  bool operator==(const PlayerRide&) const = default;
};

// ORM PLAYER_SAMPLE component 30 (oot-remake docs/TEMPLOS.md, coop/host
// ride.hpp): the moving floor (lift, rising or floating floor) that carries
// the player, by its placement hash, which is the same in every game, and
// where the player stands on it: the player's position minus the floor's, on
// the world axes. 20 bytes: u64 platform, 3 x f32 offset. The platform is
// never 0 and the offset finite.
struct Ride {
  std::uint64_t platform = 0;
  Vec3 offset{};
  bool operator==(const Ride&) const = default;
};

// PLAYER_SAMPLE. The time component is always sent; the others only when the
// sender has them. Floats must be finite.
struct PlayerSample {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, 1 to kMaxPlayers relayed
  SampleTime time;
  std::optional<Vec3> position;
  std::optional<Vec3> velocity;  // metres a second
  std::optional<Quat> rotation;
  std::optional<std::string> scene;
  std::optional<Age> age;
  std::optional<Animation> animation;
  std::optional<Ride> ride;  // ORM: carried by a moving floor (component 30)
  bool operator==(const PlayerSample&) const = default;
};

// TEST_PAYLOAD, used only by the stage 0 test tool. The filler is the rest of
// the body after the sequence number.
struct TestPayload {
  std::uint32_t sequence = 0;
  std::vector<std::uint8_t> filler;
  bool operator==(const TestPayload&) const = default;
};

// JOIN_PARTNER: a warp to another player's scene, in three steps.
//   1. The asker sends kind request, other = the player to join.
//   2. The server relays it to that player, with the asker in the player field.
//   3. That player answers with kind accept (where it stands) or refuse,
//      other = the asker; the server relays it back the same way.
// The server itself answers refuse / not_there when the player to join is
// not in the session. The request id is the asker's own; the answer echoes
// it, so a late answer to an older request can be told apart.
enum class JoinKind : std::uint8_t {
  request = 0,
  accept = 1,
  refuse = 2,
};

// Kept as a raw number on decode, so a newer build's reason still arrives.
enum class JoinRefuseReason : std::uint8_t {
  busy = 1,         // loading, in a cutscene, in a menu or dead
  declined = 2,     // the player said no
  cannot_warp = 3,  // a place nobody may warp into (a boss fight, for example)
  not_there = 4,    // sent by the server: that player is not in the session
};

// Where the answering player stands, for kind accept.
struct JoinTarget {
  std::string scene;  // the game's scene name, 1 to 64 characters
  Vec3 position{};
  Quat rotation{};  // x, y, z, w, as in PLAYER_SAMPLE
  bool operator==(const JoinTarget&) const = default;
};

struct JoinPartner {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, 1 to kMaxPlayers relayed
  std::uint8_t other_player = 1;  // 1 to kMaxPlayers: who to join, or who asked
  std::uint16_t request_id = 0;
  JoinKind kind = JoinKind::request;
  std::optional<JoinTarget> target;  // set only for kind accept
  JoinRefuseReason reason = JoinRefuseReason::busy;  // read only for kind refuse
  bool operator==(const JoinPartner&) const = default;
};

// One shared world flag. Two kinds of key (docs/protocol.md):
// - named: the Murmur3 x86 32-bit hash, seed 0, of the flag's name in the
//   game (flag_hash in codec.hpp), and for an array flag the element in
//   index (0 for a plain flag).
// - key64: a 64-bit-key flag (the game's Bool64bitKey: chests, pots, rupees,
//   Gold Skulltula tokens, enemies), keyed by the placement's 64-bit hash.
//   table names the Bool64bitKey table by its hash; 0 is the game's one
//   table, the only one so far. Always a bool.
// The fields a kind does not use are 0. The value is 4 bytes on the wire,
// read by kind.
enum class FlagKeyKind : std::uint8_t {
  named = 0,
  key64 = 1,
};

struct FlagKey {
  FlagKeyKind kind = FlagKeyKind::named;
  std::uint32_t hash = 0;   // named only
  std::uint16_t index = 0;  // named only
  std::uint64_t key = 0;    // key64 only
  std::uint32_t table = 0;  // key64 only; 0 = the game's one table

  constexpr FlagKey() = default;
  // A named flag.
  constexpr FlagKey(std::uint32_t name_hash, std::uint16_t element)
      : hash(name_hash), index(element) {}
  static constexpr FlagKey key64_flag(std::uint64_t key, std::uint32_t table = 0) {
    FlagKey k;
    k.kind = FlagKeyKind::key64;
    k.key = key;
    k.table = table;
    return k;
  }
  auto operator<=>(const FlagKey&) const = default;
};

enum class FlagKind : std::uint8_t {
  boolean = 0,   // value 0 or 1
  int32 = 1,     // two's complement
  float32 = 2,   // IEEE-754 bits, finite
  uint32 = 3,    // an enum flag's hash, or any other unsigned value
  // A change to a count (small keys, Gold Skulltula tokens, heart pieces),
  // two's complement: +1 got, -1 used. FLAG_SET only, named keys only; the
  // host adds it to its value and answers FLAG_STATE with the int32 total.
  count_delta = 4,
};

struct FlagEntry {
  FlagKey key;
  FlagKind kind = FlagKind::boolean;
  std::uint32_t value = 0;  // raw bits; see FlagKind

  constexpr FlagEntry() = default;
  constexpr FlagEntry(FlagKey k, FlagKind value_kind, std::uint32_t v)
      : key(k), kind(value_kind), value(v) {}
  // A named flag.
  constexpr FlagEntry(std::uint32_t hash, std::uint16_t index, FlagKind value_kind,
                      std::uint32_t v)
      : key(hash, index), kind(value_kind), value(v) {}
  bool operator==(const FlagEntry&) const = default;
};

// Bytes of one flag entry on the wire: u8 key kind, the key (named: u32
// hash, u16 index; key64: u64 key, u32 table), u8 kind, u32 value.
inline constexpr std::size_t kFlagEntrySize = 12;       // named
inline constexpr std::size_t kFlagEntryKey64Size = 18;  // key64
// Most entries one FLAG_SET, FLAG_STATE or FLAG_SNAPSHOT may carry. Keeps a
// message of key64 entries in the u16 body length (3600 x 18 + 5 bytes); a
// larger snapshot is split (split_snapshot).
inline constexpr std::size_t kMaxFlagEntries = 3600;

// FLAG_SET: a player's game changed shared flags. Goes to the server, which
// relays it to the host only. 1 to kMaxFlagEntries entries, each key at most
// once.
struct FlagSet {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, 1 to kMaxPlayers relayed
  std::vector<FlagEntry> flags;
  bool operator==(const FlagSet&) const = default;
};

// FLAG_STATE: the host's answer, which every player applies, the host
// included. Sent by the host; the server keeps the values and relays it to
// everyone else. Same rules for the entries as FLAG_SET.
struct FlagState {
  std::uint8_t player_number = kPlayerSelf;  // 0 from the host, 1 to kMaxPlayers relayed
  std::vector<FlagEntry> flags;
  bool operator==(const FlagState&) const = default;
};

// FLAG_RESET: the host loaded a save. The flags its load changed, as its
// game has them now, 0s included. Sent by the host only; the server keeps
// the values (replacing what it had) and relays it to everyone else, who
// write them exactly: no merging (max, OR) and no one-way rule, so the
// group's state follows the host's save (docs/flags.md, "Saves"). Same
// entries as FLAG_STATE (1 to kMaxFlagEntries, no count deltas); a longer
// reset is several messages in a row.
// FLAG_ORIGIN (server only): the player whose FLAG_SET the server just
// passed to the host. Sent to every other player but the host, before the
// host's FLAG_STATE answer, so a game can say who made the change it gets
// (the chat notices, docs/setup.md). The flags are the FLAG_SET's.
struct FlagOrigin {
  std::uint8_t player_number = kPlayerSelf;  // 1 to kMaxPlayers: who asked
  std::vector<FlagEntry> flags;
  bool operator==(const FlagOrigin&) const = default;
};

struct FlagReset {
  std::uint8_t player_number = kPlayerSelf;  // 0 from the host, 1 to kMaxPlayers relayed
  std::vector<FlagEntry> flags;
  bool operator==(const FlagReset&) const = default;
};

// SAVE_ASK: a guest asks the host for its save (docs/protocol.md, "Save
// transfer"). The server passes it to the host only, with the asker's number.
struct SaveAsk {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a guest, 1 to kMaxPlayers relayed
  bool operator==(const SaveAsk&) const = default;
};

// SAVE_PART: one part of the host's save (a packed save slot, see
// coop/host/save_transfer.hpp). Every part carries the whole transfer's
// header, so a receiver can check each one alone. Only the host sends it; the
// server passes it to `to_player`, or to every other player for 0.
inline constexpr std::uint32_t kMaxSaveBytes = 1u << 20;  // a slot is about 170 KB
inline constexpr std::size_t kMaxSavePart = 16384;
inline constexpr std::uint16_t kMaxSaveParts =
    static_cast<std::uint16_t>((kMaxSaveBytes + kMaxSavePart - 1) / kMaxSavePart);
enum class SaveReason : std::uint8_t {
  join = 1,       // a guest asked (it joined)
  host_load = 2,  // the host loaded a save: every guest reloads
};
struct SavePart {
  std::uint8_t player_number = kPlayerSelf;  // 0 from the host, 1 to kMaxPlayers relayed
  std::uint8_t to_player = 0;                // 0: every guest; 1 to kMaxPlayers: that one
  std::uint32_t transfer_id = 0;
  SaveReason reason = SaveReason::join;
  std::uint32_t total_size = 0;  // 1 to kMaxSaveBytes
  std::uint32_t crc32 = 0;       // of all total_size bytes
  std::uint16_t index = 0;       // 0 to count - 1
  std::uint16_t count = 1;       // ceil(total_size / kMaxSavePart)
  std::vector<std::uint8_t> data;  // kMaxSavePart bytes, the last part the rest
  bool operator==(const SavePart&) const = default;
};
// The parts a save of `total_size` bytes has, and part `index`'s size; 0 for
// a size out of range.
std::uint16_t save_part_count(std::uint32_t total_size);
std::size_t save_part_size(std::uint32_t total_size, std::uint16_t index);

// FLAG_SNAPSHOT: every shared flag, in parts 0 to part_count - 1, sent in
// order on the progress channel. The server sends it to a player who joins;
// a new host sends it to the server to start the table. An empty table is
// one part with no entries. 0 to kMaxFlagEntries entries per part, each key
// at most once in a part.
struct FlagSnapshot {
  std::uint16_t part = 0;
  std::uint16_t part_count = 1;  // at least 1, more than part
  std::vector<FlagEntry> flags;
  bool operator==(const FlagSnapshot&) const = default;
};

// HOST: who hosts the session, the player whose game is the truth for
// shared flags. Sent by the server on the progress channel to a player who
// joins (before its FLAG_SNAPSHOT) and to everyone when the host changes. The
// first player in hosts; when the host leaves, the remaining player with the
// lowest number takes over.
struct Host {
  std::uint8_t player_number = 1;  // 1 to kMaxPlayers
  bool operator==(const Host&) const = default;
};

// What a player wears and holds (docs/puppet.md, "Equipment"). Each name is
// the game's actor name of that piece, for example "Weapon_Sword_Kokiri" or
// "Armor_Adult_Goron"; an empty name is nothing in that slot. A field that is
// not set is unknown: the sender could not read it, and the receiver keeps
// what it had.
struct Equipment {
  std::optional<std::string> weapon;   // sword
  std::optional<std::string> shield;
  std::optional<std::string> armor;    // tunic
  std::optional<std::string> boots;
  std::optional<std::string> head;     // mask (child), or head armor
  std::optional<std::string> hand;     // gauntlets
  std::optional<std::string> ocarina;
  std::optional<std::string> arrow;    // arrow kind for the bow
  // Bit i set: the game's equipment slot i is drawn (in hand), not sheathed.
  // Slots 0 to 7; see docs/puppet.md for which slot is which.
  std::optional<std::uint8_t> drawn;
  // The item in the game's equipment slot 3 (slingshot, bow), by the name of
  // its actor; "" when the slot is empty. In hand when bit 3 of drawn is set.
  std::optional<std::string> held;
  // The same for the other hand slots: 0 (what is really in the sword hand),
  // 1, 2, 5 and 6. In hand when that slot's bit of drawn is set.
  std::optional<std::string> weapon_slot;
  std::optional<std::string> sub_weapon;
  std::optional<std::string> third_weapon;
  std::optional<std::string> tool;
  std::optional<std::string> sub_tool;
  // The player's colour, 0xRRGGBB. Unset: the player chose none (a receiver
  // uses its slot's colour, coop::protocol::slot_color).
  std::optional<std::uint32_t> color;
  // ORM: the rupees in the player's wallet (EquipComponent::rupees), 0 to
  // kMaxRupees. Each player's own (2026-11-11): sent to be shown, never
  // written into another game.
  std::optional<std::uint32_t> rupees;
  // ORM D-R19: the horse he is on, by actor name; "" on foot (EquipComponent::mount).
  std::optional<std::string> mount;
  bool operator==(const Equipment&) const = default;
};

// `now` with each field it does not carry taken from `before`: what a
// receiver keeps after a PLAYER_EQUIP, and what a sender sends after a read
// that failed for one frame.
inline Equipment merge_equipment(const Equipment& before, const Equipment& now) {
  Equipment e = now;
  auto keep = [](auto& field, const auto& old) {
    if (!field) field = old;
  };
  keep(e.weapon, before.weapon);
  keep(e.shield, before.shield);
  keep(e.armor, before.armor);
  keep(e.boots, before.boots);
  keep(e.head, before.head);
  keep(e.hand, before.hand);
  keep(e.ocarina, before.ocarina);
  keep(e.arrow, before.arrow);
  keep(e.drawn, before.drawn);
  keep(e.held, before.held);
  keep(e.weapon_slot, before.weapon_slot);
  keep(e.sub_weapon, before.sub_weapon);
  keep(e.third_weapon, before.third_weapon);
  keep(e.tool, before.tool);
  keep(e.sub_tool, before.sub_tool);
  keep(e.color, before.color);
  keep(e.rupees, before.rupees);
  keep(e.mount, before.mount);
  return e;
}

// The colour of a player who chose none: one per player number (1 to 4),
// far apart on the colour wheel. 0 (and anything above kMaxPlayers) gets white.
// ORM (six players, oot-remake docs/COLOR_JUGADOR.md): 5 and 6 get purple and
// orange from our name-tag palette, not white.
inline constexpr std::uint32_t slot_color(std::uint8_t player_number) {
  switch (player_number) {
    case 1: return 0x3070FF;  // blue
    case 2: return 0xFF3030;  // red
    case 3: return 0x30D040;  // green
    case 4: return 0xFFC020;  // yellow
    case 5: return 0xA05CE0;  // ORM: purple
    case 6: return 0xF09A28;  // ORM: orange
    default: return 0xFFFFFF;
  }
}

// PLAYER_EQUIP: a player's equipment, sent reliably on every change and again
// when another player joins. Same player field rules as PLAYER_SAMPLE.
struct PlayerEquip {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, 1 to kMaxPlayers relayed
  Equipment equipment;
  bool operator==(const PlayerEquip&) const = default;
};

// ---------------------------------------------------------------- objects
// Shared enemies and projectiles (docs/objects.md, docs/protocol.md "Objects").

enum class ObjectKind : std::uint8_t {
  placed = 0,  // value is the placement hash; maker is 0
  made = 1,    // value is the maker's own counter; maker is 1 to kMaxPlayers
};

// 10 bytes on the wire: u8 kind, u8 maker, u64 value.
struct ObjectId {
  ObjectKind kind = ObjectKind::placed;
  std::uint8_t maker = 0;
  std::uint64_t value = 0;
  auto operator<=>(const ObjectId&) const = default;
  bool operator==(const ObjectId&) const = default;
};

// OBJECT_SAMPLE component ids. 1 to 4 and 7 are laid out as in PLAYER_SAMPLE.
enum class ObjectComponent : std::uint16_t {
  time = 1,
  position = 2,
  velocity = 3,
  rotation = 4,
  animation = 7,
  life = 16,
  fuse = 17,
  state = 18,
  slots = 19,  // protocol 10, issue 150: a boss's other AS slots (below)
  carry = orm::object_component::carry,  // ORM: 29 (oot-remake docs/TIPOS_MENSAJE.md)
};
// ORM: a new upstream component here must stay below ours (orm_types.hpp).
static_assert(static_cast<std::uint16_t>(ObjectComponent::slots) == orm::kUpstreamLastObjectComponent,
              "ORM: upstream's OBJECT_SAMPLE components changed: update orm_types.hpp");

struct ObjectLife {
  std::int32_t life = 0;
  std::int32_t max = 0;
  bool operator==(const ObjectLife&) const = default;
};

namespace object_state {
inline constexpr std::uint8_t in_fight = 1u << 0;
inline constexpr std::uint8_t held = 1u << 1;
inline constexpr std::uint8_t asleep = 1u << 2;
// ORM (oot-remake docs/FINAL.md, ZMP D-231), in the owner's OBJECT_SAMPLE of a boss: its game
// needs it a while longer (Twinrova's ice holds the owner's Link, the mirror shield's part
// charge). owner_keeps: no hand-over to a hitter nor to a nearer player; owner_keeps_near: none
// to a nearer player. An unfit owner (loading, paused, downed) still gives it up. Old readers
// ignore the bits. The numbers are orm_types.hpp's (bits 7 and 6: ours count down from 7).
inline constexpr std::uint8_t owner_keeps = orm::kObjectStateOwnerKeeps;
inline constexpr std::uint8_t owner_keeps_near = orm::kObjectStateOwnerKeepsNear;
}  // namespace object_state
// ORM: our state bits count down from 7 (orm_types.hpp: kObjectStateOwnerKeeps...).
static_assert(static_cast<unsigned>(object_state::asleep) == (1u << orm::kUpstreamLastObjectStateBit),
              "ORM: upstream's object_state bits changed: update orm_types.hpp");

// ORM "Levantar" (oot-remake docs/DETALLES_MARIONETA.md): an object in a
// player's hands (a pot, a rock, a Cucco, a bomb flower), sent by its owner
// with every sample while that player's Link holds it. `holder` is the
// player whose Link holds it (1 to kMaxPlayers). `offset`/`rotation` place
// it in the frame of that Link's actor (+0x31c place, +0x328 turn), so the
// other games draw it in the hands of that player's puppet as drawn there;
// `bone`, when not kCarryNoBone, is the Link skeleton bone it was nearest
// when lifted, and `bone_offset`/`bone_rotation` place it in that bone's
// frame (used when the receiver can read the puppet's bone). An old reader
// skips the component (component ids are skipped by their length): no new
// protocol version. 59 bytes: u8, u16, 3 + 4 + 3 + 4 f32; quaternions unit.
inline constexpr std::uint16_t kCarryNoBone = 0xffff;
struct ObjectCarry {
  std::uint8_t holder = 0;
  std::uint16_t bone = kCarryNoBone;
  Vec3 offset{};
  Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};
  Vec3 bone_offset{};
  Quat bone_rotation{0.0f, 0.0f, 0.0f, 1.0f};
  bool operator==(const ObjectCarry&) const = default;
};

// Issue 150 (protocol 10): one more AS slot of the owner's actor than slot 0
// (Gohma's eye guard and eyelids play on their own slots). slot is 1 or more
// and below 32; no slot twice. An empty name: the slot plays nothing.
struct SlotAnimation {
  std::uint8_t slot = 1;
  Animation animation;
  bool operator==(const SlotAnimation&) const = default;
};
inline constexpr std::size_t kMaxObjectSlots = 8;
inline constexpr std::uint8_t kMaxSlotIndex = 31;

// The components of OBJECT_SAMPLE, OBJECT_SPAWN and HANDOVER_STATE. time is
// always sent; the others when the sender has them. Floats must be finite.
struct ObjectState {
  SampleTime time;
  std::optional<Vec3> position;
  std::optional<Vec3> velocity;  // metres a second
  std::optional<Quat> rotation;
  std::optional<Animation> animation;
  std::optional<ObjectLife> life;
  std::optional<float> fuse;          // seconds left
  std::optional<std::uint8_t> state;  // object_state bits
  std::optional<std::vector<SlotAnimation>> slots;  // issue 150: bosses only
  std::optional<ObjectCarry> carry;   // in a player's hands (ORM)
  bool operator==(const ObjectState&) const = default;
};

namespace object_presence {
inline constexpr std::uint8_t awake = 1u << 0;
inline constexpr std::uint8_t in_fight = 1u << 1;
inline constexpr std::uint8_t targeting = 1u << 2;  // targeting our player
}  // namespace object_presence

struct ObjectPresence {
  ObjectId id;
  std::uint8_t flags = 0;  // object_presence bits
  bool operator==(const ObjectPresence&) const = default;
};

// Most objects in one OBJECT_PRESENT.
inline constexpr std::size_t kMaxPresentObjects = 1024;
inline constexpr std::size_t kMaxActorNameChars = 64;

// OBJECT_PRESENT: the shared objects the sender has loaded in a scene. It
// replaces the sender's previous list for that scene. No id twice.
struct ObjectPresent {
  std::uint8_t player_number = kPlayerSelf;
  std::string scene;
  std::vector<ObjectPresence> objects;
  bool operator==(const ObjectPresent&) const = default;
};

// OBJECT_OWNER (server only): who owns an object now. owner 0: nobody.
struct ObjectOwner {
  ObjectId id;
  std::uint8_t owner = 0;
  std::uint16_t epoch = 0;
  std::uint32_t group = 0;  // 0: none
  bool operator==(const ObjectOwner&) const = default;
};

// HANDOVER_ASK (server only, to the owner): let go of it to `to_player`.
struct HandoverAsk {
  ObjectId id;
  std::uint16_t epoch = 0;
  std::uint8_t to_player = 1;
  bool operator==(const HandoverAsk&) const = default;
};

// HANDOVER_STATE: the old owner's last state, relayed to the new owner.
struct HandoverState {
  std::uint8_t player_number = kPlayerSelf;
  ObjectId id;
  std::uint16_t epoch = 0;
  ObjectState state;
  bool operator==(const HandoverState&) const = default;
};

namespace object_spawn {
inline constexpr std::uint8_t projectile = 1u << 0;
}  // namespace object_spawn

// OBJECT_SPAWN: the maker made it (kind made, maker = the sender's number).
// Owner is implied: the maker, epoch 0.
struct ObjectSpawn {
  std::uint8_t player_number = kPlayerSelf;
  ObjectId id;
  std::string actor;  // the game's actor name, up to 64 characters
  std::uint8_t flags = 0;  // object_spawn bits
  ObjectState state;
  bool operator==(const ObjectSpawn&) const = default;
};

// OBJECT_SAMPLE, on the object samples channel.
struct ObjectSample {
  std::uint8_t player_number = kPlayerSelf;
  ObjectId id;
  std::uint16_t epoch = 0;
  ObjectState state;
  bool operator==(const ObjectSample&) const = default;
};

enum class RemoveReason : std::uint8_t {
  gone = 0,
  dead = 1,
  exploded = 2,
  hit = 3,
  picked_up = 4,
};

struct ObjectRemove {
  std::uint8_t player_number = kPlayerSelf;
  ObjectId id;
  std::uint16_t epoch = 0;
  RemoveReason reason = RemoveReason::gone;
  Vec3 position{};
  bool operator==(const ObjectRemove&) const = default;
};

// Causes in DAMAGE. Other values are kept and passed on unchanged.
enum class DamageCause : std::uint8_t {
  unknown = 0,
  sword = 1,
  arrow = 2,
  fire_arrow = 3,
  ice_arrow = 4,
  light_arrow = 5,
  seed = 6,
  bomb = 7,
  nut = 8,
  boomerang = 9,
  hookshot = 10,
  hammer = 11,
  magic = 12,
};

// DAMAGE: life lost on a copy, for the owner to take off. Amount 0 or more.
struct Damage {
  std::uint8_t player_number = kPlayerSelf;
  ObjectId id;
  std::uint16_t epoch = 0;  // the epoch the hitter knew
  std::int32_t amount = 0;
  std::uint8_t cause = 0;  // DamageCause
  Vec3 position{};
  bool operator==(const Damage&) const = default;
};

// PLAYER_POSE: the sender's Link skeleton after its animation, for the
// puppet to copy bone by bone (docs/puppet.md, "Pose"). Only the bones that
// move are listed, in increasing bone order, each with the parts it carries:
// the local rotation (quaternion x, y, z, w scaled by 32767), translation
// and scale (both scaled by kPoseUnit, so +-4). flags is the game's own bone
// flag byte, passed on unread. skeleton names the skeleton (bone count and a
// check of its layout); a puppet with another skeleton ignores the message.
// A piece in Link's hands (the slingshot, the bow) goes as a pose of its own
// skeleton, every bone, and is written onto the piece of that skeleton.
inline constexpr std::size_t kMaxPoseBones = 512;
inline constexpr float kPoseUnit = 8192.0f;

namespace pose_part {
inline constexpr std::uint8_t rotation = 1u << 0;
inline constexpr std::uint8_t translation = 1u << 1;
inline constexpr std::uint8_t scale = 1u << 2;
}  // namespace pose_part

struct PoseBone {
  std::uint16_t bone = 0;
  std::uint8_t flags = 0;
  std::optional<std::array<std::int16_t, 4>> rotation;
  std::optional<std::array<std::int16_t, 3>> translation;
  std::optional<std::array<std::int16_t, 3>> scale;
  bool operator==(const PoseBone&) const = default;
};

struct PlayerPose {
  std::uint8_t player_number = kPlayerSelf;
  SampleTime time;
  std::uint32_t skeleton = 0;
  std::vector<PoseBone> bones;
  bool operator==(const PlayerPose&) const = default;
};

// OBJECT_POSE (ORM, oot-remake docs/NPC_COMPARTIDOS.md, steps 5 to 7): one
// shared object's skeleton, laid out as PLAYER_POSE's bones, for the
// townsperson a player talks to: the head turned to the talker and the face,
// which the AS slot 0 command in OBJECT_SAMPLE does not carry. Sent by the
// object's owner only, for at most one object at a time, at most 20 a second,
// on the object samples channel (unreliable). The server relays it to the
// object's scene like an OBJECT_SAMPLE (owner and epoch checked), at most one
// every 25 ms per player, and keeps nothing. A copy whose skeleton id differs
// ignores it; a pose older than the newest one of that object is dropped.
//
// Townspeople with many bones (Ruto 300, Zelda 292; oot-remake
// docs/NPC_COMPARTIDOS.md, "PNJ con muchos huesos"): one pose goes in up to
// kMaxObjectPoseParts messages, part p carrying bones p * kObjectPosePartBones
// to (p + 1) * kObjectPosePartBones - 1. All the parts of one pose have the
// same time (server time and sequence) and the same `parts` mask, the parts
// sent for that pose (bit p: part p); `part`'s own bit is in it. A part lists
// only the bones its sender sends this time (those that changed, or all of
// them on a refresh); the receiver keeps the others from earlier poses and
// shows the pose once every part in the mask came. The server relays the
// parts of one pose together (one pose every 25 ms, not one message).
inline constexpr std::size_t kObjectPosePartBones = 128;
inline constexpr std::size_t kMaxObjectPoseParts = 4;
static_assert(kObjectPosePartBones * kMaxObjectPoseParts == kMaxPoseBones);
inline constexpr std::size_t kMaxObjectPoseBones = kObjectPosePartBones;  // in one message

struct ObjectPose {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, the owner relayed
  ObjectId id;
  std::uint16_t epoch = 0;
  SampleTime time;
  std::uint32_t skeleton = 0;
  std::vector<PoseBone> bones;  // increasing bone order, all in `part`'s range
  std::uint8_t part = 0;        // below kMaxObjectPoseParts
  std::uint8_t parts = 1;       // the parts of this pose, bit p for part p; part's bit set
  bool operator==(const ObjectPose&) const = default;
};

// TIME_OF_DAY (docs/time.md): a game's clock, in seconds since midnight.
// The host sends it about once a second; any player whose clock jumped (the
// Sun's Song, a night's sleep, an event) sends it with `jump`, and every game
// takes a jump at once. Relayed to everyone else.
inline constexpr std::uint32_t kSecondsPerDay = 86'400;
namespace time_of_day {
inline constexpr std::uint8_t jump = 1u << 0;
}  // namespace time_of_day

// OCARINA_NOTE (issue 152): a note the sender's Link starts or stops on the
// Ocarina, relayed to everyone else, who play it on that player's puppet.
// `note` 0: the note held so far was let go; 1 to kOcarinaNotes: the game's
// own note table (OcarinaMgr + 0x40, the SLink assets Ocarina_LowB ...
// Ocarina_HighF in that order), plus one.
inline constexpr std::uint8_t kOcarinaNotes = 19;
struct OcarinaNote {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, 1 to kMaxPlayers relayed
  std::uint8_t note = 0;                     // 0 to kOcarinaNotes
  bool operator==(const OcarinaNote&) const = default;
};

struct TimeOfDay {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, 1 to kMaxPlayers relayed
  std::uint32_t seconds = 0;                 // 0 to kSecondsPerDay - 1
  std::uint8_t flags = 0;                    // time_of_day bits
  bool operator==(const TimeOfDay&) const = default;
};

// OBJECT_GROUP (client only, not relayed): which objects belong together, so
// the server keeps them with one owner (docs/objects.md "Groups"). Each entry
// names a member and its root: a boss part and the boss, a summoned enemy and
// its summoner. An entry whose root is the member itself takes the member
// out of any group; it is also how a lone object is marked a boss.
namespace object_group {
inline constexpr std::uint8_t boss = 1u << 0;  // the member is a boss: 60 s fight lock
}  // namespace object_group

struct ObjectGroupEntry {
  ObjectId member;
  ObjectId root;
  std::uint8_t flags = 0;  // object_group bits
  bool operator==(const ObjectGroupEntry&) const = default;
};

// FISH_LURE (client only, not relayed; issue 151, docs/objects.md "Fish and
// lures"): every fish the sender has loaded and how near the sender's own cast
// lure is to it. It replaces the sender's previous list. The server gives each
// fish to the game whose lure is nearest (a hooked fish stays with its owner),
// and a fish is never moved by the distance rule or held by a fight lock.
namespace fish_lure {
inline constexpr std::uint8_t hooked = 1u << 0;  // the sender owns it and it is on its lure
inline constexpr std::uint8_t drawn = 1u << 1;   // it swims at the sender's lure
}  // namespace fish_lure

// distance_cm when no lure of the sender's is in reach.
inline constexpr std::uint16_t kFishLureNone = 0xffff;
// Most fish in one FISH_LURE. 0 to this many, no fish twice.
inline constexpr std::size_t kMaxFishLures = 256;

struct FishLureEntry {
  ObjectId id;
  std::uint16_t distance_cm = kFishLureNone;  // fish to our nearest lure
  std::uint8_t flags = 0;                     // fish_lure bits
  bool operator==(const FishLureEntry&) const = default;
};

struct FishLure {
  std::uint8_t player_number = kPlayerSelf;
  std::vector<FishLureEntry> fish;
  bool operator==(const FishLure&) const = default;
};

// Most entries in one OBJECT_GROUP. 1 to this many, no member twice.
inline constexpr std::size_t kMaxGroupEntries = 256;

struct ObjectGroup {
  std::uint8_t player_number = kPlayerSelf;
  std::vector<ObjectGroupEntry> entries;
  bool operator==(const ObjectGroup&) const = default;
};

// GRASS_CUT (docs/world.md, "Field grass"): one cut of the field grass the
// game's foliage module draws (no actor), as the game queued it: a sphere at
// position with radius, and for a sword, three points of the swing (NaN when
// the cut is a sphere only). flags is the game's byte, passed on unread.
// Every game in the same scene makes the same cut. Relayed to everyone else.
inline constexpr float kMaxGrassCutRadius = 100.0f;
// Cuts the server keeps for one scene, for a player who enters it later.
inline constexpr std::size_t kMaxGrassCuts = 512;

struct GrassCut {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, 1 to kMaxPlayers relayed
  std::string scene;                         // the sender's scene, not empty
  Vec3 position{};
  float radius = 0.0f;                       // above 0, at most kMaxGrassCutRadius
  std::uint8_t flags = 0;
  std::array<Vec3, 3> sweep{};               // each value finite or NaN
  bool operator==(const GrassCut& o) const;  // NaN equals NaN here
};

// WORLD_BREAK (docs/world.md, "Broken for everyone"): the placed breakable
// with this placement hash was broken in the sender's scene (a pot, a crate,
// a rock, a grass tuft, a cracked wall). The server keeps each scene's
// breaks while anyone is in it and sends them to a player who enters, so a
// pot broken before a player came is gone for that player too. With
// `restored` the game put the object back (a pot that respawns): the break
// is forgotten. `kept` a break that lasts (a small-key door's lock, which the
// opener's game saves): the server keeps it for the session, also while
// nobody is in the scene, and passes on a second player's break of it too
// (both opened it: one key is given back, docs/dungeon.md "Locked doors").
namespace world_break {
inline constexpr std::uint8_t restored = 1u << 0;
inline constexpr std::uint8_t kept = 1u << 1;
}  // namespace world_break
inline constexpr std::size_t kMaxWorldKept = 256;        // kept breaks per scene
inline constexpr std::size_t kMaxWorldKeptTotal = 2048;  // and in all (the game has 98 locks)
inline constexpr std::size_t kMaxWorldBreaks = 1024;  // kept per scene

struct WorldBreak {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, 1 to kMaxPlayers relayed
  std::string scene;                         // the sender's scene, not empty
  std::uint64_t hash = 0;                    // placement hash, not 0 and not ~0
  Vec3 position{};                           // where it was (finite)
  std::uint8_t flags = 0;                    // world_break bits
  bool operator==(const WorldBreak&) const = default;
};

// WORLD_MOVE: where the sender's game has a placed object it is moving (a
// push block, a pot or rock in Link's hands or in flight), about 20 a
// second while it moves. Unreliable, relayed to the sender's scene, not kept.
namespace world_move {
inline constexpr std::uint8_t carried = 1u << 0;  // in the mover's hands
}  // namespace world_move

struct WorldMove {
  std::uint8_t player_number = kPlayerSelf;
  std::string scene;
  std::uint64_t hash = 0;
  std::uint64_t time_us = 0;  // server time of the reading
  Vec3 position{};
  Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};  // unit
  std::uint8_t flags = 0;                  // world_move bits
  bool operator==(const WorldMove&) const = default;
};

// WORLD_PLACE: the object has come to rest here after a move. Reliable; the
// server keeps the latest one of each object per scene (while anyone is in
// the scene) for a player who enters later.
inline constexpr std::size_t kMaxWorldPlaces = 256;  // kept per scene

struct WorldPlace {
  std::uint8_t player_number = kPlayerSelf;
  std::string scene;
  std::uint64_t hash = 0;
  Vec3 position{};
  Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};
  bool operator==(const WorldPlace&) const = default;
};


// WORLD_HIT (docs/world.md, "Signs"): the sender's sword swing passed the
// placed object with this hash (a sign: its life never drops, the cut is
// made by the game's destruction of the pieces the sword met). Each other
// game gives its own copy the same hit, at the same place, through the
// game's own damage call. The server keeps the last kMaxWorldHitsPerObject
// of each object per scene for a player who enters later.
inline constexpr std::size_t kMaxWorldHitsPerObject = 4;
inline constexpr std::size_t kMaxWorldHitObjects = 128;  // per scene

struct WorldHit {
  std::uint8_t player_number = kPlayerSelf;
  std::string scene;
  std::uint64_t hash = 0;
  Vec3 at{};    // where the sword was (finite)
  Vec3 from{};  // where the player who swung stood (finite)
  bool operator==(const WorldHit&) const = default;
};

// SIGN_CUT (docs/world.md, "Signs: the cut words"): the words of a sign's
// components that the sender's own sword cut changed (the sign probe saw
// them), with their new values. A replayed hit gives the others' sign the
// hit reaction but not the cut, which the blade's contact makes: so the
// cut itself is written. A key is (2 + component table entry) << 32 | byte
// offset in that component (as SignProbe::Where), an entry below
// kSignCutEntries and an offset below kSignCutBytes, a multiple of 8. The
// server keeps the latest of each sign per scene for a player who enters
// later.
inline constexpr std::size_t kMaxSignCutWords = 48;
inline constexpr std::uint64_t kSignCutEntries = 0x60;  // table +0..+0x300
inline constexpr std::uint64_t kSignCutBytes = 0x200;   // of each component

struct SignWord {
  std::uint64_t key = 0;
  std::uint64_t value = 0;
  bool operator==(const SignWord&) const = default;
};

struct SignCut {
  std::uint8_t player_number = kPlayerSelf;
  std::string scene;
  std::uint64_t hash = 0;
  std::vector<SignWord> words;  // 1 to kMaxSignCutWords, keys as above
  bool operator==(const SignCut&) const = default;
};

// Shared drops (docs/objects.md, "Drops"): the rupees, hearts and ammo an
// enemy, a pot, grass or a rock leaves. Every game announces the drops its
// own game rolls with DROP_SPAWN; where two games rolled for the same thing
// (near in place and time) the one from the owner of what dropped it stays,
// then the one from the lower player number. Relayed to everyone else.
namespace drop_spawn {
inline constexpr std::uint8_t owned = 1u << 0;  // the sender owns what dropped it
inline constexpr std::uint8_t again = 1u << 1;  // told again for a player who came later
}  // namespace drop_spawn

struct DropSpawn {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, 1 to kMaxPlayers relayed
  ObjectId id;             // kind made: the game that rolled it and its counter
  std::string scene;       // 1 to 64 characters
  std::string actor;       // the game's actor name, 1 to 64 characters
  Vec3 position{};
  std::uint64_t time_us = 0;  // server time when its maker first saw it
  std::uint8_t flags = 0;     // drop_spawn bits
  bool operator==(const DropSpawn&) const = default;
};

// DROP_TAKE: a drop is gone from the sender's game; every game removes it.
// The id is a made drop's, or (protocol 10) a placed one's: kind placed and
// the placement hash, for the loot a bush or grass tuft has in every game.
enum class DropTakeReason : std::uint8_t {
  picked_up = 0,  // the sender's Link collected it
  gone = 1,       // it blinked out
};

struct DropTake {
  std::uint8_t player_number = kPlayerSelf;
  ObjectId id;
  DropTakeReason reason = DropTakeReason::picked_up;
  bool operator==(const DropTake&) const = default;
};

// DROP_ASK: the sender just came into this scene; every game there tells
// the drops it still has again (DROP_SPAWN with `again`).
struct DropAsk {
  std::uint8_t player_number = kPlayerSelf;
  std::string scene;  // 1 to 64 characters
  bool operator==(const DropAsk&) const = default;
};

// BOSS_ROOM (docs/dungeons-boss.md): where the sender stands with a dungeon's
// boss. `in_room` is sent on entering and every 2 s while there, so a game
// in the same dungeon (one that joined or respawned too) moves its player in;
// the intro's start and end let the others leave the intro together; `left`
// and `defeated` end the pull. Relayed to everyone else.
enum class BossState : std::uint8_t {
  in_room = 1,
  intro_start = 2,
  intro_end = 3,
  left = 4,
  defeated = 5,
};
inline constexpr std::uint8_t kMaxBossRoom = 15;  // the room index in coop_boss.h's table

struct BossRoom {
  std::uint8_t player_number = kPlayerSelf;
  std::string scene;  // the dungeon, 1 to 64 characters
  std::uint8_t room = 0;  // 0 to kMaxBossRoom
  BossState state = BossState::in_room;
  bool operator==(const BossRoom&) const = default;
};

// PvP (protocol 10; docs/protocol.md, "PvP"). Off unless the host turns it
// on; the server's value (set by the host) is the whole session's.

// Most life one PvP hit may take, and most life a PVP_HURT may report:
// quarter hearts (20 hearts).
inline constexpr std::int32_t kMaxPvpLife = 80;

// PVP_MODE: whether players hurt each other in this session. From the server
// to each player when it joins and to everyone on a change: `set_by` is 0
// when the server's start decided it (coop_server --pvp), else the player
// (the host) who changed it. From a player: the host asks for a change
// (`set_by` 0; the server ignores it from anyone but the host).
struct PvpMode {
  std::uint8_t set_by = 0;  // 0 the server, 1 to kMaxPlayers a player
  bool on = false;
  bool operator==(const PvpMode&) const = default;
};

// PVP_HIT: the sender's attack reached another player's Link on the sender's
// screen. The server passes it to the victim only, and only while PvP is on;
// the victim's game decides whether it hurts (PVP_HURT).
struct PvpHit {
  std::uint8_t player_number = kPlayerSelf;  // the attacker
  std::uint8_t victim = 0;                   // 1 to kMaxPlayers, not the attacker
  std::uint8_t cause = 0;                    // DamageCause; others kept
  std::int32_t amount = 0;                   // quarter hearts, 1 to kMaxPvpLife
  Vec3 at{};    // where the blow landed
  Vec3 from{};  // where the attacker stood
  bool operator==(const PvpHit&) const = default;
};

// PVP_HURT flags.
namespace pvp_hurt {
inline constexpr std::uint8_t died = 1;     // the hit took the last of the life
inline constexpr std::uint8_t guarded = 2;  // not taken (amount 0): a cutscene, a menu, ...
inline constexpr std::uint8_t known = died | guarded;
}  // namespace pvp_hurt

// PVP_HURT: what a PVP_HIT did in the victim's game (the victim decides).
// Relayed to everyone else, so the attacker's screen shows it.
struct PvpHurt {
  std::uint8_t player_number = kPlayerSelf;  // the victim
  std::uint8_t attacker = 0;                 // 1 to kMaxPlayers, not the victim
  std::int32_t amount = 0;                   // taken: 0 to kMaxPvpLife
  std::int32_t life = 0;                     // left after it: 0 to kMaxPvpLife
  std::uint8_t flags = 0;                    // pvp_hurt; other bits refused
  bool operator==(const PvpHurt&) const = default;
};

// DUNGEON_EVENT (protocol 10, deku-dungeon-2, issues 176 and 196): a room
// gimmick's event (a switch's cutscene, as InsideTheDekuTree_Gimmick_RoomD_PlatformUp)
// started in the sender's game. The others in that scene start the same
// event at once, so its result (platforms up, a ladder down) runs at the same
// time in every game, not only after the sender's cutscene ended and its
// played flag came (docs/dungeon.md, "Live events"). Relayed to the scene,
// never kept.
inline constexpr std::size_t kMaxEventNameChars = 96;

// PLATFORM_CLOCK (protocol 10, branch platforms-b, issues 196 and 194): the
// clock word of each moving platform's driver the sender owns, read at
// `server_time_us` (server time). The others put value + rate x (their
// server time now - server_time_us) into their own word, looped by `cycle`
// when it is over 0 (docs/objects.md, "Moving platforms"). On channel 4 like
// WORLD_MOVE, unreliable: the next one comes a second later. Relayed to the
// sender's scene, never kept.
inline constexpr std::size_t kMaxPlatformTicks = 16;
inline constexpr std::uint64_t kPlatformAsFrameRegion = 0xffff;  // key region: the AS slot 0 frame
inline constexpr std::uint64_t kPlatformActorBytes = 0x600;
inline constexpr std::uint64_t kPlatformComponentBytes = 0x200;
inline constexpr std::uint64_t kPlatformComponents = 0x300 / 8;  // table entries
inline constexpr float kMaxPlatformRate = 10000.0f;
inline constexpr float kMaxPlatformValue = 1.0e7f;

struct PlatformTick {
  std::uint64_t hash = 0;  // the driver's placement hash
  // Where the word is: region << 32 | byte offset (a multiple of 4).
  // Region 0 the actor (offset under 0x600), 2 + n the component in table
  // entry n (under 0x200), kPlatformAsFrameRegion the AS slot 0 frame (offset 0).
  std::uint64_t key = 0;
  float value = 0.0f;
  float rate = 0.0f;   // per second, not 0
  float cycle = 0.0f;  // where it loops back; 0 when not seen
  bool operator==(const PlatformTick&) const = default;
};

struct PlatformClock {
  std::uint8_t player_number = kPlayerSelf;
  std::string scene;
  std::uint64_t server_time_us = 0;
  std::vector<PlatformTick> ticks;  // 1 to kMaxPlatformTicks, no hash twice
  bool operator==(const PlatformClock&) const = default;
};

// PLATFORM_COUNTER (protocol 10, branch platforms-b2): the looping pause
// counters (AI ExecuteGenericCounter nodes) of the platforms near the
// sender, each with the time since its last pause began, read at
// `server_time_us`. A game that has seen fewer starts of the same key
// shifts its own pause once to be in phase (docs/objects.md, "Moving
// platforms: the pause counters"). Channel 4, unreliable, every 3 s;
// relayed to the sender's scene, never kept.
inline constexpr std::size_t kMaxPlatformCounters = 32;
inline constexpr std::uint32_t kMinCounterPeriodUs = 100'000;

struct CounterTick {
  std::uint64_t key = 0;        // hash of the actor's placement, the pause's length and its place in the graph
  std::uint32_t period_us = 0;  // between two starts; at least kMinCounterPeriodUs
  std::uint32_t since_us = 0;   // since the last start
  std::uint32_t starts = 0;     // starts the sender has seen, at least 1
  bool operator==(const CounterTick&) const = default;
};

struct PlatformCounter {
  std::uint8_t player_number = kPlayerSelf;
  std::string scene;
  std::uint64_t server_time_us = 0;
  std::vector<CounterTick> ticks;  // 1 to kMaxPlatformCounters, no key twice
  bool operator==(const PlatformCounter&) const = default;
};

struct DungeonEvent {
  std::uint8_t player_number = kPlayerSelf;
  std::string scene;
  std::string name;  // the event's name, 1 to kMaxEventNameChars, no "HavePlayedEvent."
  bool operator==(const DungeonEvent&) const = default;
};

// REVIVE (ORM, oot-remake docs/CAIDO_REVIVIR.md, "downed, not dead"): the
// sender has held A beside a downed player (PLAYER_STATUS bit 4) long enough,
// and that player's game gets Link up. From a client, `other_player` names the
// downed player; the server passes it to that player only, with the sender's
// number in the player field (as JOIN_PARTNER), and only while that player is
// dead. `progress` kReviveDone is the request; 0 to kReviveMaxProgress is
// the hold so far in percent, sent about every 250 ms while A is held, for
// the downed player's screen (101 to 254 are refused).
inline constexpr std::uint8_t kReviveDone = 255;
inline constexpr std::uint8_t kReviveMaxProgress = 100;

struct Revive {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, the reviver relayed
  std::uint8_t other_player = 1;             // 1 to kMaxPlayers: the downed player
  std::uint8_t progress = kReviveDone;
  bool operator==(const Revive&) const = default;
};

// GROUP_DEFEAT (ORM, "downed, not dead"): the server's word that every player
// in the session (two or more) has had PLAYER_STATUS bit 4 (dead) for 2 s
// running. Sent once to everyone; again only after someone was up between.
// A game with the downed rules on lets its game over run (the player presses
// Continue and the game reloads its save, the room's at the session start).
struct GroupDefeat {
  std::uint8_t down = 1;            // players down, 1 to kMaxPlayers
  std::uint32_t server_time_ms = 0; // the server's clock (ms, wraps)
  bool operator==(const GroupDefeat&) const = default;
};

// STORY_EVENT (ORM, oot-remake docs/CINEMATICAS.md, "story cutscenes for
// everyone"): a listed story event (a cutscene, asked by its name) started by
// one player's own game. From a client: `start` when its game starts such an
// event by itself (it owns it), `end` when that event is over. The server
// relays the first `start` of a (scene, name) and its owner's `end` to the
// others, with the sender's number in the player field, and keeps the pairs
// started for the room: a player who joins later gets each as `played`, from
// the server only (with the owner's number and the start time). A game in the
// same scene that has not played it asks its own game to start the same event.
//
// `skipped` (ORM, ZMP D-048) is an end whose owner skipped the cutscene (its
// game saw his skip): the others skip what is left of their copy. A plain
// `end` never cuts anyone's copy. To a joiner the server sends a pair's
// `played` and then its owner's last word, `end` or `skipped`.
enum class StoryKind : std::uint8_t {
  start = 1,
  end = 2,
  played = 3,   // server to a joiner only
  skipped = 4,  // an end, the owner skipped it (ORM, D-048)
};
inline constexpr std::size_t kMaxStoryNameChars = 127;  // A-Z a-z 0-9 _, as the game's event names

struct StoryEvent {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, the owner relayed
  StoryKind kind = StoryKind::start;
  std::string scene;  // the owner's scene, 1 to 64 characters
  std::string name;   // the event's name, 1 to kMaxStoryNameChars
  Vec3 position{};    // the owner's Link when it started (finite)
  std::uint32_t server_time_ms = 0;  // the server's clock (ms, wraps) when it started
  bool operator==(const StoryEvent&) const = default;
};

// PLAYER_LOOK (ORM, oot-remake docs/COLOR_JUGADOR.md, "a name and a colour
// per player"): the colour a player chose, for its name tag and panel line
// (later its puppet's tunic). From a client after each WELCOME and whenever
// it changes, with 0 in the player field; the server keeps each player's
// last one, relays it to the others with the sender's number, and sends every
// player's to a joiner right after its WELCOME. `style` is reserved: 0 now; a
// reader keeps an unknown value and draws as for 0. A player with no
// PLAYER_LOOK is drawn in the default colour of its number.
using Rgb = std::array<std::uint8_t, 3>;  // red, green, blue, 0 to 255

struct PlayerLook {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, the player relayed
  Rgb rgb{};
  std::uint8_t style = 0;                    // name tag style, reserved: 0
  bool operator==(const PlayerLook&) const = default;
};

// CHEST_OPEN (ORM, oot-remake docs/DETALLES_MARIONETA.md, "Efectos";
// 2026-11-11: "cuando alguien abra un cofre debe poder ver cómo lo abre"): a
// player's own game started opening a shared chest: the chest's AS slot 0
// changed to an opening command while that player's Link stood beside it.
// The other games in that scene play the same command on their own chest of
// the same placement, at the sender's frame carried on by the time since, so
// the lid opens on every screen while the opener's Link opens it (the opened
// flag, which may come later, keeps its own path). Channel 0; the server
// relays it to everyone else with the sender's number and keeps nothing.
struct ChestOpen {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, 1 to kMaxPlayers relayed
  std::uint64_t chest = 0;          // the chest's placement hash; never 0 nor ~0
  std::string scene;                // the sender's scene, 1 to kMaxSceneChars
  Animation animation;              // the chest's command (1 to kMaxAnimationChars), frame, rate (finite)
  std::uint64_t server_time_us = 0; // when the sender read it, on the server clock
  bool operator==(const ChestOpen&) const = default;
};

// OCARINA_NOTE (ORM, oot-remake docs/OCARINA.md, section 7; issue 152): what
// a player plays on the Ocarina, so the others hear it at its puppet. From a
// client while its Link plays freely, with 0 in the player field: `note` when
// the sounding note changes, `release` when it is let go, `song` when the
// game recognises a song. The server relays it to the others with the
// sender's number, keeps nothing, and holds each player to its own rate.
// Only sound: a song played by another player changes nothing in a game (the
// flags it sets travel as shared flags). `serial` steps with each message of
// its sender; a receiver drops one with the same serial as the last.
// `instrument`, `note` and `song` are the game's own enums (main+0x3ee6ab0,
// +0x3ee6ae0, +0x3effdb0 of build e0f3ccfaf50065ae).
enum class OcarinaKind : std::uint8_t {
  note = 1,
  release = 2,
  song = 3,
};
inline constexpr std::uint8_t kMaxOcarinaInstrument = 3;  // Ocarina 0, Piccolo 1, Malon 2, Harp 3
inline constexpr std::uint8_t kMaxOcarinaNote = 19;       // 1 LowB to 19 HighF (0 Rest: not a note)
inline constexpr std::uint8_t kMaxOcarinaSong = 12;       // 0 ZeldaLullaby to 12 ScarecrowSong

struct OrmOcarinaNote {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, the player relayed
  OcarinaKind kind = OcarinaKind::note;
  std::uint8_t instrument = 0;               // 0 to kMaxOcarinaInstrument
  std::uint8_t note = 1;                     // 1 to kMaxOcarinaNote with `note`; 0 otherwise
  std::uint8_t song = 0;                     // 0 to kMaxOcarinaSong with `song`; 0 otherwise
  std::uint8_t serial = 0;                   // steps with each of the sender's messages (wraps)
  bool operator==(const OrmOcarinaNote&) const = default;
};

// GROUP_SETTINGS (ORM, oot-remake docs/AJUSTES_GRUPO.md): the settings of a
// player's game and co-op that change the shared world, as text pairs (the
// rules and the wire format: coop/protocol/orm_messages.hpp). From a client
// after each WELCOME and whenever one changes, with 0 in the player field; the
// server keeps each player's last one, relays it to the others with the
// sender's number, and sends every player's to a joiner right after its
// WELCOME, as PLAYER_LOOK.
struct SettingEntry {
  std::string key;    // a game option ("OptionData.IsUseAmiibo") or a co-op switch ("LICHII_COOP_DOWNED")
  std::string value;  // "on", "off", an option's value name, a list's fingerprint
  bool operator==(const SettingEntry&) const = default;
};

struct GroupSettings {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client, the player relayed
  std::vector<SettingEntry> entries;         // by key, each key once
  bool operator==(const GroupSettings&) const = default;
};

// LINK_NOTICE (ORM, oot-remake docs/RIESGOS_RESUELTOS.md, "Red, kit y
// alojamiento"): the relay (orm_server) tells one player something about his
// own link, so a connection that cannot keep up is never degraded silently.
// Server to client only, channel 0, never relayed. The body is frozen (u8
// kind, u32 a, u32 b): a game of another release decodes it too, which is how
// it learns the server's protocol before a REFUSED wrong_protocol_version.
// A kind this build does not know is kept as it is (the game ignores it).
enum class LinkNoticeKind : std::uint8_t {
  behind = 1,           // frames for this player are being dropped: a = percent dropped, b = KiB waiting
  caught_up = 2,        // the link keeps up again: a = seconds it was behind
  server_protocol = 3,  // a = the server's protocol version (sent before REFUSED wrong_protocol_version)
  over_rate = 4,        // the server dropped this player's own messages over its rate: a = unreliable, b = reliable
};

struct LinkNotice {
  LinkNoticeKind kind = LinkNoticeKind::behind;  // raw value kept
  std::uint32_t a = 0;
  std::uint32_t b = 0;
  bool operator==(const LinkNotice&) const = default;
};

// HOST_CHOICE (ORM, oot-remake docs/PARTIDA_SALA.md, "Un solo anfitrion"):
// one host for the relay (flags, the save transfer, the clock) and for the
// room's save. The relay picks it from what it measures (the lowest average
// round trip to the others, a steady link, a game in the world) with
// hysteresis, or a player picks it by hand (/anfitrion <name>). Channel 0, both
// ways, never relayed as it came:
// - from a client (by and host 0): `wish` (this player's own wish, after each
//   WELCOME and when it changes) or `pick` (`name` hosts from now on; an empty
//   name gives the choice back to the measures);
// - from the server: `told`, to everyone after each HOST that changed and to a
//   joiner after its HOST: `host` hosts because of `why`; `by` picked it (0:
//   the relay itself); `name` the host's nickname; `pinned` the name picked by
//   hand that the relay keeps (empty: automatic); `rtt_ms` the host's average
//   round trip to the others as measured (0: not measured). A pick the relay
//   cannot take is answered to the picker alone with why `refused` and the
//   name asked in `name`.
// Unknown kinds and whys are kept as they came (a game ignores them).
enum class HostChoiceKind : std::uint8_t {
  wish = 1,
  pick = 2,
  told = 3,
};

enum class HostWish : std::uint8_t {
  automatic = 0,  // the relay decides
  me = 1,         // this player would rather host (the app's toggle)
  never = 2,      // never this player while another can (room_save_host = false)
};

enum class HostWhy : std::uint8_t {
  first = 1,     // the first player in
  left = 2,      // the host left: the best of the others
  measured = 3,  // a clearly better link for a while
  unfit = 4,     // the host's game was out of the world or its link unsteady for a while
  picked = 5,    // a player picked it by hand
  wished = 6,    // a player who asked to host (or the host asked not to)
  refused = 7,   // to the picker alone: that pick cannot be taken (`name`: the name asked)
};

struct HostChoice {
  HostChoiceKind kind = HostChoiceKind::told;  // raw value kept
  HostWish wish = HostWish::automatic;         // wish; raw value kept
  HostWhy why = HostWhy::first;                // told; raw value kept
  std::uint8_t host = 0;                       // told: 1 to kMaxPlayers
  std::uint8_t by = 0;                         // told: who picked, 0: the relay
  std::uint16_t rtt_ms = 0;                    // told: the host's measured average, 0: none
  std::string name;                            // pick: a nickname or empty; told: the host's (or the one asked)
  std::string pinned;                          // told: the name picked by hand, empty: automatic
  bool operator==(const HostChoice&) const = default;
};

// ---- ORM orm/pvp (oot-remake docs/COMANDOS.md): four session messages, wire
// formats in coop/protocol/orm_messages.hpp. ----

// ROOM_SWITCH: a switch that holds for the whole room, set live by any
// player (chat command, the Cooperativo screen). From a client with 0 in the
// player and time fields; the server keeps the room's last value of each key,
// stamps who set it and when (its clock), relays a change to everyone (the
// sender too, as the answer) and sends the room's switches to a joiner after
// its WELCOME. A key this build does not know is kept and relayed as it is.
enum class RoomSwitchKey : std::uint8_t {
  pvp = 1,         // weapons of one Link hurt the others (off: they do nothing to mates)
  lock_mates = 2,  // Z-targeting locks on mates as on enemies
  bodies = 3,      // ORM choque: each mate's puppet has a real body (oot-remake docs/CHOQUE.md)
};
inline constexpr std::uint8_t kMaxRoomSwitchKey = 31;  // keys 1 to 31
inline constexpr std::size_t kMaxRoomSwitches = 8;
struct RoomSwitchEntry {
  std::uint8_t key = 1;  // RoomSwitchKey, 1 to kMaxRoomSwitchKey
  bool on = false;
  bool operator==(const RoomSwitchEntry&) const = default;
};
struct RoomSwitch {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client; from the server: who set it
  std::uint64_t server_time_us = 0;          // 0 from a client; from the server: when it took it
  std::vector<RoomSwitchEntry> entries;      // 1 to kMaxRoomSwitches, keys increasing (each once)
  bool operator==(const RoomSwitch&) const = default;
};

// PLAYER_HIT: the attacker's game saw its weapon reach a mate's puppet while
// the room's PvP switch is on. To the victim only (the server checks the
// switch, the scene and a rate); the victim's game decides (shield, its
// invulnerability, a repeat) and applies the damage and the push to its own
// Link: the victim owns his life.
enum class HitWeapon : std::uint8_t {
  sword = 1,
  arrow = 2,
  seed = 3,
  hookshot = 4,
  boomerang = 5,
  magic = 6,
  hammer = 7,
  nut = 8,
  stick = 9,
  other = 10,
};
inline constexpr std::uint8_t kMaxHitWeapon = 10;
inline constexpr std::uint8_t kMaxHitDamage = 80;  // quarter hearts (20 hearts)
inline constexpr float kMaxHitPush = 3.0f;         // metres
namespace player_hit {
inline constexpr std::uint8_t pull = 0x01;       // the push is toward the attacker (the hookshot)
inline constexpr std::uint8_t push_only = 0x02;  // no damage (a Deku Nut's stun, a shove)
}  // namespace player_hit
struct PlayerHit {
  std::uint8_t player_number = kPlayerSelf;  // the attacker; 0 from a client
  std::uint8_t victim = 1;                   // 1 to kMaxPlayers, never the attacker
  std::uint32_t hit_id = 0;                  // the attacker's own counter, never 0
  HitWeapon weapon = HitWeapon::sword;
  std::uint8_t damage = 0;                   // quarter hearts, 0 to kMaxHitDamage
  std::uint8_t flags = 0;                    // player_hit bits (others kept)
  Vec3 from{};                               // the attacker's Link, finite
  Vec3 push{};                               // world metres, at most kMaxHitPush long
  bool operator==(const PlayerHit&) const = default;
};

// PLAYER_CALL: "come to me". `come` goes to one mate (`to`), who may accept
// (/aceptar: his game asks the usual JOIN_PARTNER teleport to the caller);
// `gather` is the host's to everyone (`to` 0). The server relays it with the
// sender's number; a `gather` from a player who does not host is dropped.
enum class CallKind : std::uint8_t {
  come = 1,
  gather = 2,
};
struct PlayerCall {
  std::uint8_t player_number = kPlayerSelf;  // the caller; 0 from a client
  std::uint8_t to = 0;                       // come: 1 to kMaxPlayers; gather: 0
  CallKind kind = CallKind::come;
  std::uint32_t call_id = 0;                 // the caller's own counter
  bool operator==(const PlayerCall&) const = default;
};

// PLAYER_RENAME: a player's new name while playing. From a client with 0
// and result ok; the server checks it (the app's rule, and no other player
// of the room has it, ignoring ASCII case), keeps it for joiners and
// reconnections, and sends it with the player's number and ok to everyone,
// or back to the asker alone with the reason.
enum class RenameResult : std::uint8_t {
  ok = 0,
  name_taken = 1,
  bad_name = 2,
};
inline constexpr std::uint8_t kMaxRenameResult = 2;
// The app's rule for a name: 1 to kMaxNicknameChars of A-Z a-z 0-9 . _ -
bool valid_new_nickname(std::string_view name);
struct PlayerRename {
  std::uint8_t player_number = kPlayerSelf;  // 0 from a client; the renamed player
  RenameResult result = RenameResult::ok;
  std::string nickname;                      // valid_new_nickname
  bool operator==(const PlayerRename&) const = default;
};

using Message = std::variant<Hello, Welcome, Refused, PlayerJoined, PlayerLeft,
                             Ping, Pong, PlayerStatus, Chat, PlayerSample,
                             TestPayload, JoinPartner, FlagSet, FlagState,
                             FlagSnapshot, Host, PlayerEquip, ObjectPresent,
                             ObjectOwner, HandoverAsk, HandoverState, ObjectSpawn,
                             ObjectSample, ObjectRemove, Damage, PlayerPose,
                             TimeOfDay, GrassCut, ObjectGroup, WorldBreak, WorldMove,
                             WorldPlace, WorldHit, DropSpawn, DropTake,
                             DropAsk, BossRoom, SignCut, FlagReset, FlagOrigin,
                             JoinChallenge, JoinProof, SaveAsk, SavePart, PlayerLife,
                             PvpMode, PvpHit, PvpHurt, FishLure, RejoinToken, RejoinClaim, DungeonEvent, PlayerRide,
                             OcarinaNote, PlatformClock, PlatformCounter,
                             Revive, GroupDefeat, StoryEvent, PlayerLook,
                             ObjectPose, ChestOpen, OrmOcarinaNote, GroupSettings, LinkNotice,
                             HostChoice, RoomSwitch, PlayerHit, PlayerCall, PlayerRename>;

MessageType type_of(const Message& message);

// The channel a message is sent on. TEST_PAYLOAD may go on 0 or 3; this
// returns 3 and the test tool picks for itself.
std::uint8_t channel_of(const Message& message);
std::uint8_t channel_of(MessageType type);

// Whether a message of this type may arrive on this channel. False for a type
// this build does not know.
bool allowed_on_channel(MessageType type, std::uint8_t channel);

// The two ends of a connection.
enum class Sender : std::uint8_t {
  client,
  server,
};

// Whether this side may send this message type. HELLO is client only;
// WELCOME, REFUSED, PLAYER_JOINED, PLAYER_LEFT and HOST are server only; the rest go
// both ways. False for a type this build does not know.
bool may_send(Sender from, MessageType type);

}  // namespace coop::protocol

#pragma pop_macro("slots")
