# The wire protocol: plain bytes in, plain structs out. No ENet, so the
# server, the client, Lichii and the recomp can all share it.
add_library(coop_protocol STATIC
  protocol/src/codec.cpp
  protocol/src/join_password.cpp
  protocol/src/traffic_crypto.cpp
  protocol/src/orm_messages.cpp  # ORM: GROUP_SETTINGS
)
target_include_directories(coop_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/protocol/include")
target_link_libraries(coop_protocol PRIVATE monocypher coop_warnings)
if(WIN32)
  target_link_libraries(coop_protocol PRIVATE bcrypt)  # BCryptGenRandom (join_password.cpp)
endif()

# The client over ENet that the test tool, Lichii and the recomp use. It also
# carries the WebSocket transport, selected in ClientConfig.
add_library(coop_net STATIC
  net/src/clock.cpp
  net/src/client.cpp
  net/src/enet_transport.cpp
  net/src/ws_transport.cpp
)
target_include_directories(coop_net PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/net/include")
target_link_libraries(coop_net PUBLIC coop_protocol PRIVATE enet coop_boost coop_openssl coop_warnings)
if(MSVC)
  # Asio's TLS templates need more object sections than the default allows.
  target_compile_options(coop_net PRIVATE /bigobj)
endif()
if(WIN32)
  target_compile_definitions(coop_net PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  target_link_libraries(coop_net PRIVATE ws2_32)
endif()

# The co-op library: protocol and networking, filled in by later stage 0 steps.
add_library(coop STATIC
  src/version.cpp
)
target_include_directories(coop PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(coop PUBLIC enet coop_protocol coop_net PRIVATE coop_warnings)

# Presence: where to draw the other players, from their decoded messages.
# No sockets and no game, so Lichii and the recomp can both call it.
add_library(coop_presence STATIC
  presence/src/presence.cpp
)
target_include_directories(coop_presence PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/presence/include")
target_link_libraries(coop_presence PUBLIC coop_protocol PRIVATE coop_warnings)

# Host: reads the local player from guest memory (through an interface the
# emulator implements) and builds PLAYER_SAMPLE and PLAYER_STATUS. No sockets.
add_library(coop_host STATIC
  host/src/age.cpp
  host/src/boss_rules.cpp
  host/src/chest_open.cpp
  host/src/continue_load.cpp
  host/src/countdown_search.cpp
  host/src/clock.cpp
  host/src/doors.cpp
  host/src/downed.cpp
  host/src/mate_down_sound.cpp
  host/src/dungeon_lists.cpp
  host/src/final_rules.cpp
  host/src/epona.cpp
  host/src/flags.cpp
  host/src/flag_watcher.cpp
  host/src/game_flags.cpp
  host/src/game_items.cpp
  host/src/item_rules.cpp
  host/src/gerudo.cpp
  host/src/notify.cpp
  host/src/game_memory.cpp
  host/src/group_exit.cpp
  host/src/hearts.cpp
  host/src/held_switch.cpp
  host/src/hazards.cpp
  host/src/live_check.cpp
  host/src/pause_detect.cpp
  host/src/npc_talk.cpp
  host/src/passages.cpp
  host/src/flag_defer.cpp
  host/src/flag_burst.cpp
  host/src/orm_link_watch.cpp
  host/src/player_reader.cpp
  host/src/sampler.cpp
  host/src/save_transfer.cpp
  host/src/session.cpp
  host/src/songs.cpp
  host/src/story.cpp
  host/src/trades.cpp
  host/src/beans.cpp
  host/src/prizes.cpp
  host/src/wallets.cpp
  host/src/water.cpp
  host/src/time_temple.cpp
  host/src/worn_armor.cpp
  host/src/orm_group_settings.cpp  # ORM: the group's settings (oot-remake docs/AJUSTES_GRUPO.md)
  host/src/host_choice.cpp  # ORM: one host for the room (oot-remake docs/PARTIDA_SALA.md)
  host/src/orm_commands.cpp  # ORM orm/pvp: chat commands (oot-remake docs/COMANDOS.md)
  host/src/orm_pvp.cpp
  host/src/orm_as_slots.cpp  # ORM PvP shield: every AS slot of our Link (oot-remake docs/PVP.md)
  host/src/orm_bodies.cpp  # ORM orm/cuerpos-continuar: out of a mate's body, move by move
  host/src/orm_stats.cpp
)
target_include_directories(coop_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host/include")
# Session (session.hpp) ties the client, the sampler, the flag watcher and
# presence together, so the host library also links those two.
target_link_libraries(coop_host PUBLIC coop_protocol coop_net coop_presence PRIVATE coop_warnings)

# Overlay: the other players as markers with name tags, as a plain draw list.
# No rendering and no game addresses; the caller hands over the camera.
add_library(coop_overlay STATIC
  overlay/src/overlay.cpp
)
target_include_directories(coop_overlay PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/overlay/include")
# Header only: the scene rules (coop/host/scene.hpp), so a marker shows in the
# same places as a puppet.
target_include_directories(coop_overlay PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/host/include")
target_link_libraries(coop_overlay PUBLIC coop_presence PRIVATE coop_warnings)
