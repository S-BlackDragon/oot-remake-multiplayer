// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM (oot-remake docs/DETALLES_MARIONETA.md, "Mascaras, sedal y cadena"):
// three things another player's puppet did not show.
//
// - Masks (and the Lens of Truth). The piece is made (PLAYER_EQUIP's head,
//   the worn armor piece since orm/detalles) but drawn elsewhere: a mask's
//   model is bound to its wearer by bone name (SameBoneModelBindParam
//   "RotTrans": it takes the local transforms of the wearer's bones of the
//   same name: Skl_Root, Spine_1, Spine_2, Neck, Head, Head_2), so it is drawn
//   at its own actor's place with the wearer's head pose, and nobody puts
//   that actor on the puppet (the tunics, which show, are bound by world
//   matrix). Heads::Follow, each calc frame after the pose copy: the head
//   piece (armor link 0, or hand slot 7 "Helmet") whose model is a mask or a
//   lens gets the puppet's place and turn (and the engine's pending
//   transform, as the puppet), and the puppet's local transforms of every
//   bone of the same name are copied onto it, in case the bind does not run
//   for a puppet. A separate actor, then: no new protocol.
// - The fishing line. On the fisher it is an actor of its own,
//   Weapon_FishingLine (model Item_FishingRod_Rope: Root, Rope_002 to Rope_070,
//   Tip, 0.25 apart along +x), a Phive rope: neither a hand piece nor a pose
//   (71 bones). Here it is procedural: the lure copy (CoopShot_Lure, issue
//   151) brings a companion copy, CoopShot_FishingLine (the rope's model only,
//   tools/puppet_pack STAND_INS), placed at the lure, whose bones are laid
//   each frame from the lure to the tip of the rod in the owner's puppet's
//   hands (bone Rod_4 of the Item_FishingRod piece), with a small sag.
// - The hookshot chain. It is the tip's own model (Shootable_Hook's
//   Item_Hookshot_Chain: Skl_Root, the claws, MainLine_00 to MainLine_59
//   0.383 apart, a SubLine under each), stretched on the shooter by the
//   tip's AS (HookshotChainRateApplyAS); the stand-in copy has no AS, so its
//   chain stood at rest length. Each frame the copy's MainLine bones are laid
//   from the tip to the hookshot in the owner's puppet's hands (bone
//   FirePos_HookShot or FirePos_LongHook of the Item_Hookshot_Body piece):
//   whole links from the tip, the rest folded at the hand. The model's own
//   segments are placed and scaled, so no segment actors are spawned.
//
// The bone hierarchy comes from the game's skeleton resource in memory (the
// relocated FSKL the skeleton object points to: bone names and parents), the
// local transforms from the layout CoopPose reads and writes. How the three
// stored rows of a local rotation act (Rows) is not proven: it is told once
// from a puppet's spine (DetectRows: the head above the hips), else the
// default. Pure math and fake-memory tests: tests/lichii/mascaras_test.cpp.
// Switches: LICHII_ORM_MASK_FOLLOW, LICHII_ORM_TETHERS (both on by default).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CoopPuppet {
class Guest;
}

namespace CoopMascaras {

using Vec3 = std::array<float, 3>;
// A 3x3 matrix by rows, column vectors (p' = M p): as CoopPuppet::Mat3 and
// the actor's +0x328.
using Mat3 = std::array<float, 9>;
inline constexpr Mat3 kIdentity{1, 0, 0, 0, 1, 0, 0, 0, 1};

namespace addr {
// The skeleton object (CoopPose::addr::kSkeletonPath's end) points at +0 to
// its skeleton resource: the FSKL of the model file, relocated (offsets made
// pointers, as FMDL +0x8 is the model's name). Its bones: an array of
// kBoneStride at +0x10, their count (u16) at +0x38; a bone's name pointer
// (u16 length and the characters) at +0, its parent (i16, -1 for the root)
// at +0x22. Read from the romfs's .bfres files (Player_Child 309 bones,
// Item_Hookshot_Chain 128, Item_FishingRod_Rope 71); not yet read in memory.
inline constexpr std::uint64_t kSkeletonResource = 0x0;
inline constexpr std::uint64_t kFsklBones = 0x10;
inline constexpr std::uint64_t kFsklBoneCount = 0x38;
inline constexpr std::uint64_t kBoneStride = 0x58;
inline constexpr std::uint64_t kBoneName = 0x00;
inline constexpr std::uint64_t kBoneParent = 0x22;
// The transform bits of a local transform's flags (bits 23 to 30, the byte
// CoopPose copies with a pose), as nn::g3d's bone flags lay them out: scale
// one, uniform scale, volume one, no rotation, no translation, and three
// "the whole hierarchy above too" bits. A bone we place gets the first five
// from what it holds and the three hierarchy bits cleared (they would let the
// game skip a transform that is no longer identity).
inline constexpr std::uint32_t kScaleOne = 1u << 23;
inline constexpr std::uint32_t kScaleUniform = 1u << 24;
inline constexpr std::uint32_t kScaleVolumeOne = 1u << 25;
inline constexpr std::uint32_t kRotZero = 1u << 26;
inline constexpr std::uint32_t kTransZero = 1u << 27;
inline constexpr std::uint32_t kHierarchyBits = 0x7u << 28;
inline constexpr std::uint32_t kTransformBits = 0xffu << 23;
}  // namespace addr

// One bone's local transform as the game holds it: CoopPose's layout (0x60
// bytes: flags, scale at +0x10, three rows of 4 at +0x20, translation at +0x50).
struct Local {
    std::uint32_t flags = 0;
    std::uint32_t unused[3]{};
    float scale[4]{1, 1, 1, 0};
    float rows[12]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    float translation[4]{};
};
static_assert(sizeof(Local) == 0x60);

// How the three stored rows turn a child's point into its parent's:
// axes: row i is the child's axis i in the parent (nn::util's 4x3, row
//   vectors: the translation is the fourth row); the matrix is their transpose.
// matrix: the rows are the matrix's rows (column vectors, as the actor's).
enum class Rows : std::uint8_t { axes, matrix };
const char* RowsText(Rows rows);

// A bone's place in its model: p_model = rot * p_bone + origin. `rot` has the
// bones' scales in it (they are about 1).
struct Frame {
    Mat3 rot = kIdentity;
    Vec3 origin{};
};

// ---------------------------------------------------------------- math
Mat3 Mul(const Mat3& a, const Mat3& b);
Vec3 Apply(const Mat3& m, const Vec3& v);
Mat3 Transposed(const Mat3& m);
float Length(const Vec3& v);
// The nearest rotation (Gram-Schmidt on the columns, x first); identity when
// degenerate.
Mat3 Orthonormal(const Mat3& m);

// The bone's rotation and scale as a column-vector matrix, and back (a
// rotation, with scale 1).
Mat3 ToParent(const Local& local, Rows rows);
void SetRotation(Local& local, const Mat3& rotation, Rows rows);
// The bone's frame in its model, through its parents. A parent index out of
// range or a loop ends the walk (treated as the model root).
Frame BoneFrame(const std::vector<Local>& locals, const std::vector<std::int16_t>& parents,
                std::size_t bone, Rows rows);
// A Link skeleton's convention, from its spine: the one that puts `head`
// kHeadAbove over `hips` and within kHeadAside of the line above them. None
// when both or neither does (crawling, a skeleton that is no Link's).
// ORM riesgos-visual: `fits` (when given) says how many of the two ways put
// the head there: 1 decides; 0 or 2 (a puppet lying down, or turned so both
// read upright) leave the last answer, and RowsCheck says so once.
inline constexpr float kHeadAbove = 0.15f;
inline constexpr float kHeadAside = 0.2f;
std::optional<Rows> DetectRows(const std::vector<Local>& locals,
                               const std::vector<std::int16_t>& parents, std::size_t head,
                               std::size_t hips, int* fits = nullptr);

// One chain bone's local transform: rotation (column vectors) and translation.
struct ChainBone {
    Mat3 rot = kIdentity;
    Vec3 t{};
};
// `from` turned the least that takes its x axis onto `dir` (parallel
// transport, so a chain does not twist). `dir` of length 0: `from`.
Mat3 TurnXTo(const Mat3& from, const Vec3& dir);
// The local transforms that put chain bone k's origin on points[k], its x
// axis along the chain (to points[k+1]; the last as the one before), for a
// chain whose first bone's parent is `parent` (its rotation is made
// orthonormal first).
std::vector<ChainBone> ChainLocals(const Frame& parent, const std::vector<Vec3>& points);
// The hookshot chain's points: `count` links of `link` from `start` toward
// `end`, the ones past `end` folded onto it.
std::vector<Vec3> HookPoints(const Vec3& start, const Vec3& end, float link, std::size_t count);
// The fishing line's points: `count` (2 or more) from `start` to `end`, sagging
// along `down` (a unit vector) by `sag` times the span across `down` at the
// middle, a parabola.
std::vector<Vec3> LinePoints(const Vec3& start, const Vec3& end, const Vec3& down,
                             std::size_t count, float sag);
// Writes a chain bone into a local transform (scale 1) with its transform bits.
void Store(Local& local, const ChainBone& bone, Rows rows);
// The five transform bits a local transform should carry (scale one,
// uniform, volume one, no rotation, no translation).
std::uint32_t TransformBits(const Local& local);

// ---------------------------------------------------------------- guest
// An actor's skeleton: its local transforms and, from the FSKL, every bone's
// name and parent. Nothing when any of it cannot be read or the counts differ.
struct Skeleton {
    std::uint64_t local = 0;  // the local transform array
    std::uint64_t fskl = 0;   // the skeleton resource
    std::uint32_t count = 0;
    std::string model;        // the FMDL's name ("" when not readable)
    std::vector<std::string> names;
    std::vector<std::int16_t> parents;
    int Find(std::string_view name) const;  // -1 none
};
// `names` false: the local array, count and model only (a cheap check that a
// skeleton read before is still the same).
std::optional<Skeleton> ReadSkeleton(CoopPuppet::Guest& guest, std::uint64_t actor,
                                     bool names = true);
bool ReadLocals(CoopPuppet::Guest& guest, const Skeleton& skel, std::vector<Local>& out);
bool WriteLocals(CoopPuppet::Guest& guest, const Skeleton& skel, const std::vector<Local>& locals);

// The convention the guest code uses: DetectRows' last answer, else
// kDefaultRows. Any thread (an atomic).
inline constexpr Rows kDefaultRows = Rows::axes;
// ORM riesgos-visual (docs/RIESGOS_RESUELTOS.md): the self-check line of the
// rows (CoopVCheck "bone rows"): ok once a puppet's spine decided it, with
// how; fallback once when a reading did not decide and nothing had before
// (the default "axes" is used meanwhile). `fits` as DetectRows says it,
// `up`: the head over the hips the decided way.
void RowsCheck(int fits, std::optional<Rows> rows, float up);
Rows CurrentRows();
// Returns true when it changed what CurrentRows says.
bool NoteRows(Rows rows);
void Reset();  // tests: forget the convention and the cached skeletons

// ---------------------------------------------------------------- masks
// Head pieces whose model is bound by bone name (masks, the Lens of Truth),
// by their model's or actor's name; never the default head.
bool IsBoundHead(std::string_view name);

class Heads {
public:
    using Log = std::function<void(const std::string&)>;
    // Calc thread, once a frame after the pose copy, for live puppet `number`
    // (actor `puppet`): its bound head piece placed on it and given its
    // bones of the same name. False when it has none (or it cannot be read).
    bool Follow(CoopPuppet::Guest& guest, std::uint8_t number, std::uint64_t puppet,
                const Log& log);
    void Forget(std::uint8_t number) { seen_.erase(number); }
    void Clear() { seen_.clear(); }
    // The head piece of `puppet` (armor link 0, else hand slot 7), present; 0 none.
    static std::uint64_t HeadPiece(CoopPuppet::Guest& guest, std::uint64_t puppet);

private:
    struct Seen {
        std::uint64_t piece = 0;
        std::uint64_t piece_local = 0;
        std::uint64_t puppet_local = 0;
        std::uint32_t piece_count = 0;
        std::uint32_t puppet_count = 0;
        bool bound = false;  // its model is a mask or a lens
        bool names = false;  // both skeletons' bone names were read
        std::string model;
        std::vector<std::pair<std::uint16_t, std::uint16_t>> map;  // piece bone, puppet bone
        bool said = false;
    };
    std::map<std::uint8_t, Seen> seen_;
};

// ---------------------------------------------------------------- tethers
enum class Tether : std::uint8_t { none, hook_chain, fishing_line };
const char* TetherText(Tether tether);
// The fishing line's own key in CoopSpawn: a pseudo shared actor (never on
// the shared list, never made as the game's own: stand-in only) whose stand-in
// is CoopShot_FishingLine.
inline constexpr std::string_view kLineActor = "Weapon_FishingLine";
// Which tether a shared actor's copy carries: the hookshot tips their chain,
// kLineActor the line. A lure carries none itself (its line is the companion).
Tether TetherOf(std::string_view shared_actor);
// The bones of the piece in the puppet's hands where it starts, best first.
std::vector<std::string_view> AnchorBones(std::string_view shared_actor);
// The puppet's hand bones, tried when no piece has those bones.
inline constexpr std::array<std::string_view, 2> kHandBones{"Weapon_R", "Weapon_L"};
// A piece's place counts as in the hand within this of a hand bone; else the
// bone is taken from the hand's frame (the piece's actor may stand at the
// wearer's root).
inline constexpr float kPieceOnHand = 0.5f;

struct Anchor {
    Vec3 at{};
    const char* from = "";  // "piece", "hand frame", "hand"; string literals only
    std::string bone;
    float off = 0.0f;       // the piece's own place from the nearest hand bone
};
// Where the tether of `shared_actor`'s copy starts on `puppet`, in the world.
// Notes the rows convention from the puppet's spine on the way.
std::optional<Anchor> FindAnchor(CoopPuppet::Guest& guest, std::uint64_t puppet,
                                 std::string_view shared_actor);
// The calc thread: the anchor on the live puppet of player `owner`
// (CoopPuppet::TheDirector's slots); nothing while puppets are off.
std::optional<Anchor> PuppetAnchor(CoopPuppet::Guest& guest, std::uint8_t owner,
                                   std::string_view shared_actor);
using AnchorSource = std::function<std::optional<Anchor>(CoopPuppet::Guest&, std::uint8_t owner,
                                                         std::string_view shared_actor)>;

// Lays the chain or line of tether copies, calc thread.
class Tethers {
public:
    using Log = std::function<void(const std::string&)>;
    // An anchor further than this from the copy is a stale or wrong place: folded.
    static constexpr float kMaxReach = 40.0f;
    static constexpr float kLineSag = 0.05f;
    // Copy `key` (actor `actor`, placed at `at` turned `rot`) laid from itself
    // to `anchor`; with no anchor (its puppet is not here) or one further than
    // kMaxReach, folded onto itself, so no chain or line at rest length shows.
    // False when nothing was written (no chain bones, not readable). One line
    // per copy says what was found, and one when it is first folded.
    bool Lay(CoopPuppet::Guest& guest, std::uint64_t key, std::uint64_t actor, Tether tether,
             const Vec3& at, const Mat3& rot, const std::optional<Anchor>& anchor,
             const Log& log);
    // Copies no longer live are forgotten.
    void Keep(const std::vector<std::uint64_t>& keys);
    std::size_t Count() const { return known_.size(); }

private:
    struct Known {
        std::uint64_t actor = 0;
        std::uint64_t local = 0;
        Skeleton skel;
        std::vector<std::uint16_t> chain;  // chain bones, from the copy outward
        int parent = -1;                   // the first chain bone's parent; -1 the model
        float link = 0.0f;                 // a hook link's rest length
        bool usable = false;
        bool said = false;       // what was found
        bool folded_said = false;
        bool laid_said = false;
    };
    bool Learn(CoopPuppet::Guest& guest, Known& k, std::uint64_t actor, Tether tether);
    std::map<std::uint64_t, Known> known_;
};

}  // namespace CoopMascaras
