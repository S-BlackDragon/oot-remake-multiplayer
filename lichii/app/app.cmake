# Included by lichii/hook/qt.cmake when LICHII_COOP is on: the "Cooperativo"
# screen of the single app (oot-remake docs/APP_UNICA.md, piece C) built into
# the Qt frontend. It only shows when orm-app.ini is beside the exe; the main
# window calls it through lichii/patches/0102-orm-app.patch.
set(_orm_app "${LICHII_COOP_DIR}/lichii/app")
target_sources(yuzu PRIVATE
    "${_orm_app}/orm_app.cpp"
    "${_orm_app}/orm_app.h"
    "${_orm_app}/orm_app_model.cpp"
    "${_orm_app}/orm_app_model.h"
    "${_orm_app}/orm_app_window.cpp"
    "${_orm_app}/orm_app_window.h"
    "${_orm_app}/orm_helper_client.cpp"
    "${_orm_app}/orm_helper_client.h"
)
target_include_directories(yuzu PRIVATE "${_orm_app}")
# Which co-op commit is built in, for the screen's version lines (as
# hook/core.cmake does for the log's "coop: build ...").
execute_process(COMMAND git -C "${LICHII_COOP_DIR}" describe --tags --always --dirty
    OUTPUT_VARIABLE _orm_app_coop_version OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT _orm_app_coop_version)
    set(_orm_app_coop_version "unknown")
endif()
set_property(SOURCE "${_orm_app}/orm_app_window.cpp" APPEND PROPERTY
    COMPILE_DEFINITIONS "ORM_APP_COOP_VERSION=\"${_orm_app_coop_version}\"")
