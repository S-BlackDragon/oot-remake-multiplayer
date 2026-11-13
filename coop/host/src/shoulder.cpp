#include "coop/host/shoulder.hpp"

namespace coop::host {

void ShoulderArbiter::set_invite(std::uint64_t bits) {
  if (bits == invite_) return;
  invite_ = bits;
  if (quick_on_ && bits != 0 && (bits & combo_.button()) != 0) {
    notes_.push_back(
        "shoulder: the quick messages and the invitation share a button: held alone it is the "
        "invitation, a d-pad press while it is held is a quick message and cancels the invitation "
        "until the button is let go");
  }
}

void ShoulderArbiter::step(std::optional<std::uint64_t> buttons) {
  if (buttons == buttons_) return;  // a second reader this frame: nothing new
  buttons_ = buttons;
  if (!buttons) {
    combo_.reset();
    cancelled_ = false;
    return;
  }
  const std::uint64_t b = *buttons;
  if (quick_on_) {
    if (const auto q = combo_.step(b)) {
      quick_ = q;
      if (shared() && !cancelled_) {
        cancelled_ = true;
        notes_.push_back(std::string("shoulder: ") + std::string(quick_chat::entry(*q).dpad_name) +
                         " while the button is held: a quick message; the invitation's hold is cancelled "
                         "until the button is let go");
      }
    }
  }
  if ((b & combo_.button()) == 0) cancelled_ = false;
}

bool ShoulderArbiter::invite_held() const {
  if (invite_ == 0 || !buttons_ || (*buttons_ & invite_) != invite_) return false;
  return !(shared() && cancelled_);
}

std::uint64_t ShoulderArbiter::mask() const {
  if (!quick_on_ || !buttons_) return 0;
  return combo_.mask(*buttons_);
}

}  // namespace coop::host
