#pragma once
// ORM: one button, two uses (docs/RIESGOS_RESUELTOS.md "Mundo e historia"). The quick messages
// (coop/host/quick_chat.hpp, orm/comunicacion2: hold L and press the d-pad) and the warp-song
// invitation (coop/host/songs.hpp WarpInvite: hold L for 0.75 s) share L by default. The rule:
// - L held alone (no d-pad press since it went down) is the invitation's hold;
// - a d-pad press while L is held is a quick message, and it cancels the invitation's hold until
//   L is let go (one press never does both);
// - a d-pad already down when L goes down is neither (as Combo: nothing is sent).
// When the two use different buttons nothing is arbitrated. When the game itself turns out to use
// L (InviteButton's check, "invitation button L: the game uses it"), the invitation takes its
// fallback button too (set_invite with both bits); the quick messages keep L + d-pad, whose d-pad
// is kept from the game.
//
// step() may be called several times a frame with the same buttons (the quick chat glue and the
// songs glue both read the pad): only a change of the buttons does anything, so the result does
// not depend on who calls first. Plain buttons in, no clock.
// Test: tests/host/shoulder_test.cpp.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "coop/host/quick_chat.hpp"

namespace coop::host {

class ShoulderArbiter {
 public:
  explicit ShoulderArbiter(std::uint64_t quick_button = quick_chat::kL) : combo_(quick_button) {}

  // Quick messages on or off (LICHII_ORM_QUICK_CHAT).
  void set_quick(bool on) { quick_on_ = on; }
  // The buttons the invitation's hold needs, all down (L, or L and its fallback); 0: none.
  void set_invite(std::uint64_t bits);

  // The pad's buttons now; nothing: no pad (everything held is forgotten).
  void step(std::optional<std::uint64_t> buttons);

  // The quick message a d-pad press made, once (for the quick chat glue).
  std::optional<quick_chat::Quick> take_quick() { return std::exchange(quick_, std::nullopt); }
  // The invitation's hold now (for WarpInvite's 0.75 s).
  bool invite_held() const;
  // The d-pad bits kept from the game now (quick messages on and their button held).
  std::uint64_t mask() const;
  // Lines for the log ("shoulder: ..."; the glue adds "coop: ").
  std::vector<std::string> take_notes() { return std::exchange(notes_, {}); }

  bool cancelled() const { return cancelled_; }
  std::uint64_t quick_button() const { return combo_.button(); }
  bool shared() const { return quick_on_ && invite_ != 0 && (invite_ & combo_.button()) != 0; }

 private:
  quick_chat::Combo combo_;
  bool quick_on_ = false;
  std::uint64_t invite_ = 0;
  std::optional<std::uint64_t> buttons_;  // the last step's
  bool cancelled_ = false;                // a quick message this hold: no invitation until let go
  std::optional<quick_chat::Quick> quick_;
  std::vector<std::string> notes_;
};

}  // namespace coop::host
