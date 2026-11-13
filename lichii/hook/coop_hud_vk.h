// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM in-game HUD, the emulator side (orm_hud.h): draws OrmHud::Current()
// into the finished frame, after the window adapt pass, as solid rectangles
// with vkCmdClearAttachments in a render pass of our own that loads the
// frame. No shader, pipeline, buffer or descriptor. Built into video_core by
// hook/video.cmake (patch 0100-orm-hud), called by the patched BlitScreen::DrawToFrame
// for the window and the screenshot blits (not the applet capture).
#pragma once

#include <utility>
#include <vector>

#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Layout {
struct FramebufferLayout;
}

namespace Vulkan {
class Device;
class Scheduler;
struct Frame;
} // namespace Vulkan

namespace CoopHudVk {

class Pass {
public:
    // `frame` has just been drawn by the window adapt pass: its framebuffer
    // (made for that pass's render pass, which ours is compatible with) holds
    // the picture in GENERAL layout. `layout.screen` is where the game's
    // picture is in it. Does nothing while the HUD is off or has nothing to
    // show.
    void Draw(const Vulkan::Device& device, Vulkan::Scheduler& scheduler,
              const Vulkan::Frame& frame, const Layout::FramebufferLayout& layout,
              VkFormat format);

private:
    VkRenderPass RenderPass(const Vulkan::Device& device, VkFormat format);

    // One per frame format seen, kept until the blit goes: a format change
    // never destroys a render pass that recorded work may still use.
    std::vector<std::pair<VkFormat, Vulkan::vk::RenderPass>> passes_;
};

} // namespace CoopHudVk
