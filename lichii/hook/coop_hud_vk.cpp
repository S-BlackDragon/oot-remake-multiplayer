// SPDX-License-Identifier: GPL-3.0-or-later
#include "coop_hud_vk.h"

#include <algorithm>
#include <array>

#include "core/frontend/framebuffer_layout.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_present_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"

#include "orm_hud.h"

namespace CoopHudVk {
namespace {

// Consecutive rectangles of one colour: one vkCmdClearAttachments each, in
// the HUD's order (later ones cover earlier ones).
struct Batch {
    VkClearAttachment attachment{};
    std::vector<VkClearRect> rects;
};

VkClearAttachment Colour(std::uint32_t rgb) {
    VkClearAttachment a{};
    a.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    a.colorAttachment = 0;
    a.clearValue.color.float32[0] = static_cast<float>((rgb >> 16) & 0xff) / 255.0f;
    a.clearValue.color.float32[1] = static_cast<float>((rgb >> 8) & 0xff) / 255.0f;
    a.clearValue.color.float32[2] = static_cast<float>(rgb & 0xff) / 255.0f;
    a.clearValue.color.float32[3] = 1.0f;
    return a;
}

} // namespace

VkRenderPass Pass::RenderPass(const Vulkan::Device& device, VkFormat format) {
    for (const auto& [f, pass] : passes_) {
        if (f == format) {
            return *pass;
        }
    }
    // As the present passes' own (present/util.cpp CreateWrappedRenderPass),
    // so the frame's framebuffer fits it, but loading what is there, and
    // waiting for the window adapt pass's colour writes.
    const VkAttachmentDescription attachment{
        .flags = VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT,
        .format = format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
        .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    constexpr VkAttachmentReference colour_ref{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkSubpassDescription subpass{
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colour_ref,
        .pResolveAttachments = nullptr,
        .pDepthStencilAttachment = nullptr,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = nullptr,
    };
    const std::array dependencies{
        VkSubpassDependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        },
        // What reads the frame next (the present manager's copy to the
        // swapchain, frame generation, the screenshot download) sets its own
        // barrier, as after the window adapt pass; this only makes our writes
        // available to it.
        VkSubpassDependency{
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dependencyFlags = 0,
        },
    };
    auto pass = device.GetLogical().CreateRenderPass(VkRenderPassCreateInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = static_cast<u32>(dependencies.size()),
        .pDependencies = dependencies.data(),
    });
    const VkRenderPass handle = *pass;
    passes_.emplace_back(format, std::move(pass));
    return handle;
}

void Pass::Draw(const Vulkan::Device& device, Vulkan::Scheduler& scheduler,
                const Vulkan::Frame& frame, const Layout::FramebufferLayout& layout,
                VkFormat format) {
    if (!OrmHud::On() || !frame.framebuffer || frame.width == 0 || frame.height == 0) {
        return;
    }
    const auto model = OrmHud::Current();
    if (!model) {
        return;
    }
    const OrmHud::Box picture{
        static_cast<std::int32_t>(layout.screen.left),
        static_cast<std::int32_t>(layout.screen.top),
        static_cast<std::int32_t>(layout.screen.GetWidth()),
        static_cast<std::int32_t>(layout.screen.GetHeight()),
    };
    const auto rects = OrmHud::Draw(*model, picture, static_cast<int>(frame.width),
                                     static_cast<int>(frame.height));
    if (rects.empty()) {
        return;
    }
    std::vector<Batch> batches;
    std::uint32_t last = 0;
    for (const auto& r : rects) {
        if (batches.empty() || r.rgb != last) {
            batches.push_back({Colour(r.rgb), {}});
            last = r.rgb;
        }
        batches.back().rects.push_back(VkClearRect{
            .rect = {.offset = {r.x, r.y},
                     .extent = {static_cast<u32>(r.width), static_cast<u32>(r.height)}},
            .baseArrayLayer = 0,
            .layerCount = 1,
        });
    }
    const VkRenderPass render_pass = RenderPass(device, format);
    const VkFramebuffer framebuffer = *frame.framebuffer;
    const VkExtent2D extent{frame.width, frame.height};
    scheduler.Record([render_pass, framebuffer, extent,
                      batches = std::move(batches)](Vulkan::vk::CommandBuffer cmdbuf) {
        Vulkan::BeginRenderPass(cmdbuf, render_pass, framebuffer, extent);
        for (const auto& b : batches) {
            cmdbuf.ClearAttachments(b.attachment, Vulkan::vk::Span<VkClearRect>(
                                                      b.rects.data(), b.rects.size()));
        }
        cmdbuf.EndRenderPass();
    });
}

} // namespace CoopHudVk
