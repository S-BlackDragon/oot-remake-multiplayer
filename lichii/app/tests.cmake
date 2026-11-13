# The single app's unit tests (lichii/app/tests), included by tests/CMakeLists.txt.
# They need Qt 6 Core only (no widgets); without Qt (CMAKE_PREFIX_PATH or Qt6_DIR
# not set) they are skipped, so the usual test build is unchanged.
find_package(Qt6 6.5 QUIET COMPONENTS Core)
if(NOT Qt6Core_FOUND)
  message(STATUS "orm_app_tests: Qt 6 Core not found, skipped (set CMAKE_PREFIX_PATH to a Qt 6 folder)")
  return()
endif()
set(_orm_app "${CMAKE_CURRENT_LIST_DIR}")
add_executable(orm_app_tests
  "${_orm_app}/tests/orm_app_tests.cpp"
  "${_orm_app}/orm_helper_client.cpp"
  "${_orm_app}/orm_app_model.cpp"
)
target_include_directories(orm_app_tests PRIVATE "${_orm_app}")
target_link_libraries(orm_app_tests PRIVATE Qt6::Core doctest::doctest coop_warnings)
# The tests run the program itself as the fake helper: Qt's DLLs must be found.
get_target_property(_orm_qt_core Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT _orm_qt_core)
  get_target_property(_orm_qt_core Qt6::Core LOCATION)
endif()
get_filename_component(_orm_qt_bin "${_orm_qt_core}" DIRECTORY)
add_test(NAME orm_app_tests COMMAND orm_app_tests)
set_tests_properties(orm_app_tests PROPERTIES
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_orm_qt_bin}" TIMEOUT 120)
