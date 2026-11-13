# Included by the patched src/video_core/CMakeLists.txt when LICHII_COOP is on
# (patch 0100-orm-hud). Adds the in-game HUD's Vulkan pass (coop_hud_vk.cpp) to the
# emulator's video_core library; the HUD's model and layout are in core
# (core.cmake). Changing what this builds needs no new patch.
set(_coop_hook "${LICHII_COOP_DIR}/lichii/hook")
target_sources(video_core PRIVATE
    "${_coop_hook}/coop_hud_vk.cpp"
)
