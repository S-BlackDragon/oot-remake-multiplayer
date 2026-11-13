# A ';' in a doctest name splits the name for ctest: doctest_discover_tests
# then registers a test that runs nothing. Refuse such names at configure time.
file(GLOB_RECURSE coop_test_sources CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
foreach(source IN LISTS coop_test_sources)
  file(STRINGS "${source}" bad_names REGEX "(TEST_CASE|SUBCASE|SCENARIO|TEST_CASE_FIXTURE)[(]\"[^\"]*;[^\"]*\"")
  if(bad_names)
    message(FATAL_ERROR "A test name in ${source} has a ';' (ctest would run nothing for it): ${bad_names}")
  endif()
endforeach()

add_executable(coop_tests
  main.cpp
  smoke_test.cpp
  protocol/codec_test.cpp
  protocol/fuzz_test.cpp
  protocol/join_password_test.cpp
  protocol/traffic_crypto_test.cpp
  net/clock_test.cpp
  net/client_test.cpp
  net/small_path_test.cpp
  net/encrypt_loopback_test.cpp
  net/client_rtt_test.cpp
  net/ws_transport_test.cpp
  presence/presence_test.cpp
  host/reader_test.cpp
  host/sampler_test.cpp
  host/flags_test.cpp
  host/flag_watcher_test.cpp
  host/game_flags_test.cpp
  host/game_items_test.cpp
  host/notify_test.cpp
  host/save_transfer_test.cpp
  host/session_test.cpp
  host/scene_test.cpp
  host/npc_talk_test.cpp
  host/downed_test.cpp
  host/mate_down_sound_test.cpp
  host/age_test.cpp
  host/continue_load_test.cpp
  host/countdown_search_test.cpp
  host/story_test.cpp
  host/time_temple_test.cpp
  host/dungeon_lists_test.cpp
  host/kit_lists_test.cpp
  host/group_exit_test.cpp
  host/hearts_test.cpp
  host/hazards_test.cpp
  host/gerudo_test.cpp
  host/ride_test.cpp
  host/flag_defer_test.cpp
  host/item_rules_test.cpp
  host/final_rules_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_final.cpp
  host/clock_test.cpp
  host/pose_smooth_test.cpp
  host/water_test.cpp
  host/held_switch_test.cpp
  host/epona_test.cpp
  host/songs_test.cpp
  host/live_check_test.cpp
  host/pause_detect_test.cpp
  host/trades_test.cpp
  host/beans_test.cpp
  host/prizes_test.cpp
  host/flag_burst_test.cpp
  host/passages_test.cpp
  host/doors_test.cpp
  host/chest_open_test.cpp
  host/menus_test.cpp
  host/worn_armor_test.cpp
  overlay/overlay_test.cpp
  lichii/camera_test.cpp
  lichii/camera_aim_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_camera_aim.cpp
  lichii/spectator_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_spectator.cpp
  lichii/npc_gaze_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_npc_gaze.cpp
  lichii/settings_test.cpp
  lichii/puppet_test.cpp
  lichii/puppet_config_test.cpp
  lichii/callers_test.cpp
  lichii/effects_test.cpp
  lichii/fairy_test.cpp
  lichii/rooms_test.cpp
  lichii/epona_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_epona.cpp
  lichii/bodies_hook_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_bodies.cpp  # ORM choque
  lichii/objects_test.cpp
  lichii/hits_test.cpp
  lichii/carry_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_carry.cpp
  lichii/spawn_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_spawn.cpp
  lichii/ride_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_ride.cpp
  lichii/mascaras_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_mascaras.cpp
  lichii/projectiles_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_projectiles.cpp
  lichii/enemy_shots_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_enemy_shots.cpp
  lichii/drops_test.cpp
  lichii/enemies_test.cpp
  lichii/guard_test.cpp
  lichii/snap_test.cpp
  lichii/scrub_talk_test.cpp
  lichii/chests_test.cpp
  lichii/colors_test.cpp
  lichii/dungeon_test.cpp
  lichii/platforms_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_platforms.cpp
  lichii/counters_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_counters.cpp
  lichii/quest_test.cpp
  lichii/story_test.cpp
  lichii/quests_test.cpp
  lichii/adult_list_test.cpp
  lichii/minigame_list_test.cpp
  lichii/town_list_test.cpp
  lichii/montana_list_test.cpp
  lichii/collectible_list_test.cpp
  lichii/trade_list_test.cpp
  lichii/pose_test.cpp
  lichii/interest_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_interest.cpp
  lichii/npc_pose_test.cpp
  lichii/world_test.cpp
  lichii/doors_test.cpp
  lichii/chest_open_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_chest_open.cpp
  lichii/ocarina_test.cpp
  lichii/ocarina_sound_test.cpp
  orm/orm_ocarina_sound_test.cpp  # ORM: notes and songs at the puppets (was lichii/ocarina_sound_test.cpp)
  lichii/ocarina_note_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_ocarina_note.cpp
  lichii/pickups_test.cpp
  lichii/testinput_test.cpp
  lichii/time_test.cpp
  orm/story_events_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/orm_story_events.cpp
  orm/switch_test.cpp
  orm/wallet_test.cpp
  host/flag_probe_test.cpp
  host/shoulder_test.cpp
  orm/quick_chat_test.cpp  # ORM quick messages (docs/COMUNICACION.md)
  orm/group_settings_test.cpp
  orm/group_settings_codec_test.cpp
  orm/group_settings_server_test.cpp
  orm/link_watch_test.cpp
  orm/host_choice_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_host_choice.cpp  # ORM one host
  orm/commands_test.cpp  # ORM orm/pvp (oot-remake docs/COMANDOS.md)
  orm/pvp_test.cpp
  orm/bodies_test.cpp  # ORM orm/cuerpos-continuar
  orm/stats_test.cpp
  orm/pvp_codec_test.cpp
  orm/pvp_server_test.cpp
  orm/app_channel_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/orm_app_channel.cpp
  # ORM cases once added to upstream's test files (oot-remake D-R65, ADOPTAR_UPSTREAM.md rule 2)
  orm/host_flag_watcher_test.cpp
  orm/host_flags_test.cpp
  orm/host_game_flags_test.cpp
  orm/host_reader_test.cpp
  orm/host_sampler_test.cpp
  orm/host_session_test.cpp
  orm/lichii_boss_test.cpp
  orm/lichii_dungeon_test.cpp
  orm/lichii_objects_test.cpp
  orm/lichii_ocarina_test.cpp
  orm/lichii_pose_test.cpp
  orm/lichii_puppet_test.cpp
  orm/lichii_settings_test.cpp
  orm/lichii_spawn_test.cpp
  orm/lichii_testinput_test.cpp
  orm/net_client_test.cpp
  orm/overlay_test.cpp
  orm/presence_test.cpp
  orm/protocol_codec_test.cpp
  lichii/every_test.cpp
  lichii/hud_test.cpp
  lichii/platform_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_frame_log.cpp
  lichii/perf_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_perf.cpp  # ORM: LICHII_ORM_PERF
  orm/orm_hud_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/orm_hud.cpp  # ORM: the in-frame HUD (was lichii/hud_test.cpp)
  lichii/hud_settings_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_hud_settings.cpp
  lichii/map_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_map.cpp
  lichii/game_build_test.cpp
  lichii/game_scan_test.cpp
  lichii/boss_test.cpp
  lichii/boss_rules_test.cpp
  lichii/teleport_test.cpp
  lichii/pvp_test.cpp
  lichii/resume_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_resume.cpp
  lichii/escape_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_escape.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_escape_timer.cpp
  lichii/live_events_test.cpp
  lichii/shared_scenes_test.cpp
  lichii/scrubs_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_scrubs.cpp
  lichii/held_switch_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_held_switch.cpp
  lichii/ride_hook_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/orm_ride.cpp  # ORM moving floors
  lichii/temples_list_test.cpp
  lichii/cheat_items_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_cheat_items.cpp
  # These hook files have no emulator code, so they are tested on their own.
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_boss.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_callers.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_camera.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_drops.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_effects.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_enemies.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_chests.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_colors.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_dungeon.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_quest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_story.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_quests.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_live_events.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_live_places.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_shared_scenes.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_fairy.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_rooms.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_objects_sync.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_hits.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_copy_smooth.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_pose.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_npc_pose.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_puppet.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_puppet_config.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_doors.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_detalles.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_settings.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_world.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_ocarina.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_ocarina_sound.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_puppet_sound.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/orm_ocarina_sound.cpp  # ORM
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_pickups.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_testinput.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_teleport.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_pvp.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook/coop_time_sync.cpp
)
target_include_directories(coop_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../lichii/hook)
# The Hookshot module and our patch to Hookshot (hookshot/): what can be checked without the game.
target_sources(coop_tests PRIVATE hookshot/module_test.cpp hookshot/draw_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../hookshot/src/coop_draw.cpp
  hookshot/chatbox_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../hookshot/src/coop_chatbox.cpp
  hookshot/cheats_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../hookshot/src/coop_cheats_panel.cpp
  hookshot/png_test.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../hookshot/src/coop_png.cpp)
target_include_directories(coop_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../hookshot/src)
# The WebSocket transport's header lives beside its source, not in the public
# include folder; its test drives it directly.
target_include_directories(coop_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../coop/net/src)
target_link_libraries(coop_tests PRIVATE coop coop_presence coop_host coop_overlay doctest::doctest coop_warnings)
target_link_libraries(coop_tests PRIVATE coop_boost coop_openssl)
if(MSVC)
  # The TLS WebSocket server in the transport tests needs the same as coop_net.
  target_compile_options(coop_tests PRIVATE /bigobj)
endif()
if(WIN32)
  target_compile_definitions(coop_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  target_link_libraries(coop_tests PRIVATE ws2_32)
endif()
# The session tests run the real server in process.
target_link_libraries(coop_tests PRIVATE coop_server_lib)
doctest_discover_tests(coop_tests)

add_subdirectory(server)

# The single app's helper client and status model (Qt 6 Core; skipped without Qt).
include(${CMAKE_CURRENT_SOURCE_DIR}/../lichii/app/tests.cmake)

# The test tool runs a server and two clients for 3 s and loses nothing.
add_test(NAME coop_test_two_clients COMMAND coop_test --clients 2 --seconds 3 --port 0 --report-every 0)

# Where the test data files are (source paths are trimmed out of __FILE__, cmake/trim_paths.cmake).
target_compile_definitions(coop_tests PRIVATE COOP_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
