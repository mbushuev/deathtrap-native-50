cmake_minimum_required(VERSION 3.21)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" DEATHTRAP_NATIVE_VERSION
  LIMIT_COUNT 1)
string(STRIP "${DEATHTRAP_NATIVE_VERSION}" DEATHTRAP_NATIVE_VERSION)
if(NOT DEATHTRAP_NATIVE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must contain a semantic version such as 0.0.0")
endif()

project(deathtrap_native50_overlay VERSION ${DEATHTRAP_NATIVE_VERSION}
  LANGUAGES C CXX)

add_executable(widescreen_layout_test tools/widescreen_layout_test.cc)
target_compile_features(widescreen_layout_test PRIVATE cxx_std_17)
target_include_directories(widescreen_layout_test PRIVATE src)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Deathtrap Native 50 requires 32-bit Windows and MSVC")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Configure with -A Win32; DD_CD.EXE is a 32-bit process")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY
  "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
  minhook
  URL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(minhook)

configure_file(
  src/deathtrap_native_version.rc.in
  "${CMAKE_CURRENT_BINARY_DIR}/deathtrap_native_version.rc"
  @ONLY
)

configure_file(src/native50_menu_assets.rc.in
  "${CMAKE_CURRENT_BINARY_DIR}/native50_menu_assets.rc" @ONLY)

add_library(deathtrap_native_dinput SHARED
  src/deathtrap_native_render_patch.cc
  src/dinput_native_proxy.cc
  src/dinput_native_proxy.def
  "${CMAKE_CURRENT_BINARY_DIR}/deathtrap_native_version.rc"
  "${CMAKE_CURRENT_BINARY_DIR}/native50_menu_assets.rc"
  src/native_d3d11_present_guard.cc
)
target_compile_features(deathtrap_native_dinput PRIVATE cxx_std_17)
target_compile_definitions(deathtrap_native_dinput PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  UNICODE
  _UNICODE
  DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}"
)
target_compile_options(deathtrap_native_dinput PRIVATE
  /W4 /permissive- /EHsc /utf-8)
target_link_options(deathtrap_native_dinput PRIVATE
  /IGNORE:4222)
target_link_libraries(deathtrap_native_dinput PRIVATE
  d3d11 dxgi dxguid minhook winmm)
set_target_properties(deathtrap_native_dinput PROPERTIES
  OUTPUT_NAME "dinput"
  PREFIX ""
)

add_executable(dinput_proxy_smoke_test
  tools/dinput_proxy_smoke_test.cc)
target_compile_features(dinput_proxy_smoke_test PRIVATE cxx_std_17)
target_compile_definitions(dinput_proxy_smoke_test PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  UNICODE
  _UNICODE
  DIRECTINPUT_VERSION=0x0700
)
target_compile_options(dinput_proxy_smoke_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)
target_link_libraries(dinput_proxy_smoke_test PRIVATE dinput8 dxguid)

add_executable(input_device_routing_test
  tools/input_device_routing_test.cc
  src/deathtrap_native_render_patch.cc
  src/native_d3d11_present_guard.cc)
target_compile_features(input_device_routing_test PRIVATE cxx_std_17)
target_compile_definitions(input_device_routing_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(input_device_routing_test PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(input_device_routing_test PRIVATE d3d11 dxgi dxguid minhook winmm)

add_executable(controller_base_bindings_test
  tools/controller_base_bindings_test.cc
  src/dinput_native_proxy.cc
  src/native_d3d11_present_guard.cc)
target_compile_features(controller_base_bindings_test PRIVATE cxx_std_17)
target_compile_definitions(controller_base_bindings_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(controller_base_bindings_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)
target_link_libraries(controller_base_bindings_test PRIVATE
  d3d11 dxgi dxguid minhook winmm)

add_executable(camera_spring_arm_test
  tools/camera_spring_arm_test.cc)
target_compile_features(camera_spring_arm_test PRIVATE cxx_std_17)
target_include_directories(camera_spring_arm_test PRIVATE src)
target_compile_options(camera_spring_arm_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

add_executable(game_text_test
  tools/game_text_test.cc
  src/dinput_native_proxy.cc
  src/native_d3d11_present_guard.cc)
target_compile_features(game_text_test PRIVATE cxx_std_17)
target_compile_definitions(game_text_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(game_text_test PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(game_text_test PRIVATE
  d3d11 dxgi dxguid minhook winmm)

add_executable(controller_ui_test
  tools/controller_ui_test.cc
  src/dinput_native_proxy.cc
  src/native_d3d11_present_guard.cc)
target_compile_features(controller_ui_test PRIVATE cxx_std_17)
target_compile_definitions(controller_ui_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(controller_ui_test PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(controller_ui_test PRIVATE
  d3d11 dxgi dxguid minhook winmm)

add_executable(camera_room_collision_test
  tools/camera_room_collision_test.cc)
target_compile_features(camera_room_collision_test PRIVATE cxx_std_17)
target_include_directories(camera_room_collision_test PRIVATE src)
target_compile_options(camera_room_collision_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

add_executable(modern_control_menu_test
  tools/modern_control_menu_test.cc
  src/dinput_native_proxy.cc
  src/native_d3d11_present_guard.cc)
target_compile_features(modern_control_menu_test PRIVATE cxx_std_17)
target_compile_definitions(modern_control_menu_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(modern_control_menu_test PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(modern_control_menu_test PRIVATE d3d11 dxgi dxguid minhook winmm)

add_executable(native50_menu_test
  tools/native50_menu_test.cc
  src/dinput_native_proxy.cc
  src/native_d3d11_present_guard.cc
  "${CMAKE_CURRENT_BINARY_DIR}/native50_menu_assets.rc")
target_compile_features(native50_menu_test PRIVATE cxx_std_17)
target_compile_definitions(native50_menu_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(native50_menu_test PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(native50_menu_test PRIVATE d3d11 dxgi dxguid minhook winmm)

add_executable(render_queue_test
  tools/render_queue_test.cc
  src/dinput_native_proxy.cc
  src/native_d3d11_present_guard.cc)
target_compile_features(render_queue_test PRIVATE cxx_std_17)
target_compile_definitions(render_queue_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(render_queue_test PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(render_queue_test PRIVATE d3d11 dxgi dxguid minhook winmm)

add_executable(dual_heroine_test
  tools/dual_heroine_test.cc
  src/dinput_native_proxy.cc
  src/native_d3d11_present_guard.cc)
target_compile_features(dual_heroine_test PRIVATE cxx_std_17)
target_compile_definitions(dual_heroine_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(dual_heroine_test PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(dual_heroine_test PRIVATE d3d11 dxgi dxguid minhook winmm)

add_executable(texture_pool_test
  tools/texture_pool_test.cc
  src/dinput_native_proxy.cc
  src/native_d3d11_present_guard.cc)
target_compile_features(texture_pool_test PRIVATE cxx_std_17)
target_compile_definitions(texture_pool_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(texture_pool_test PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(texture_pool_test PRIVATE d3d11 dxgi dxguid minhook winmm)

add_executable(heroine_waist_test
  tools/heroine_waist_test.cc
  src/dinput_native_proxy.cc
  src/native_d3d11_present_guard.cc)
target_compile_features(heroine_waist_test PRIVATE cxx_std_17)
target_compile_definitions(heroine_waist_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(heroine_waist_test PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(heroine_waist_test PRIVATE d3d11 dxgi dxguid minhook winmm)

add_executable(corpse_retention_test
  tools/corpse_retention_test.cc
  src/dinput_native_proxy.cc
  src/native_d3d11_present_guard.cc)
target_compile_features(corpse_retention_test PRIVATE cxx_std_17)
target_compile_definitions(corpse_retention_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(corpse_retention_test PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(corpse_retention_test PRIVATE d3d11 dxgi dxguid minhook winmm)

add_executable(immersive_first_person_test
  tools/immersive_first_person_test.cc)
target_compile_features(immersive_first_person_test PRIVATE cxx_std_17)
target_include_directories(immersive_first_person_test PRIVATE src)
target_compile_options(immersive_first_person_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

add_executable(music_track_routing_test
  tools/music_track_routing_test.cc)
target_compile_features(music_track_routing_test PRIVATE cxx_std_17)
target_include_directories(music_track_routing_test PRIVATE src)
target_compile_options(music_track_routing_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

add_executable(mouse_combat_routing_test
  tools/mouse_combat_routing_test.cc)
target_compile_features(mouse_combat_routing_test PRIVATE cxx_std_17)
target_include_directories(mouse_combat_routing_test PRIVATE src)
target_compile_options(mouse_combat_routing_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

add_executable(keyboard_camera_relative_test
  tools/keyboard_camera_relative_test.cc)
target_compile_features(keyboard_camera_relative_test PRIVATE cxx_std_17)
target_include_directories(keyboard_camera_relative_test PRIVATE src)
target_compile_options(keyboard_camera_relative_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

add_executable(controller_movement_test tools/controller_movement_test.cc)
target_compile_features(controller_movement_test PRIVATE cxx_std_17)
target_include_directories(controller_movement_test PRIVATE src)
target_compile_options(controller_movement_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

add_executable(input_command_bindings_test
  tools/input_command_bindings_test.cc)
target_compile_features(input_command_bindings_test PRIVATE cxx_std_17)
target_include_directories(input_command_bindings_test PRIVATE src)
target_compile_options(input_command_bindings_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

add_executable(input_binding_pages_test
  tools/input_binding_pages_test.cc)
target_compile_features(input_binding_pages_test PRIVATE cxx_std_17)
target_include_directories(input_binding_pages_test PRIVATE src)
target_compile_options(input_binding_pages_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

add_executable(safe_save_test
  tools/safe_save_test.cc)
target_compile_features(safe_save_test PRIVATE cxx_std_17)
target_include_directories(safe_save_test PRIVATE src)
target_compile_options(safe_save_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

add_executable(selector_time_dilation_test
  tools/selector_time_dilation_test.cc)
target_compile_features(selector_time_dilation_test PRIVATE cxx_std_17)
target_include_directories(selector_time_dilation_test PRIVATE src)
target_compile_options(selector_time_dilation_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

add_executable(timing_diagnostics_test tools/timing_diagnostics_test.cc)
target_compile_features(timing_diagnostics_test PRIVATE cxx_std_17)
target_include_directories(timing_diagnostics_test PRIVATE src)
target_compile_options(timing_diagnostics_test PRIVATE /W4 /permissive- /EHsc /utf-8)

add_executable(timing_diagnostics_runtime_test
  tools/timing_diagnostics_runtime_test.cc
  src/dinput_native_proxy.cc
  src/native_d3d11_present_guard.cc)
target_compile_features(timing_diagnostics_runtime_test PRIVATE cxx_std_17)
target_compile_definitions(timing_diagnostics_runtime_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(timing_diagnostics_runtime_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)
target_link_libraries(timing_diagnostics_runtime_test PRIVATE
  d3d11 dxgi dxguid minhook winmm)

add_executable(selector_gameplay_command_test
  tools/selector_gameplay_command_test.cc)
target_compile_features(selector_gameplay_command_test PRIVATE cxx_std_17)
target_include_directories(selector_gameplay_command_test PRIVATE src)
target_compile_options(selector_gameplay_command_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

add_executable(selector_combat_runtime_test
  tools/selector_combat_runtime_test.cc
  src/dinput_native_proxy.cc
  src/native_d3d11_present_guard.cc)
target_compile_features(selector_combat_runtime_test PRIVATE cxx_std_17)
target_compile_definitions(selector_combat_runtime_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(selector_combat_runtime_test PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(selector_combat_runtime_test PRIVATE d3d11 dxgi dxguid minhook winmm)

add_executable(selector_camera_handoff_test
  tools/selector_camera_handoff_probe.cc)
target_compile_features(selector_camera_handoff_test PRIVATE cxx_std_17)
target_include_directories(selector_camera_handoff_test PRIVATE src)
target_compile_options(selector_camera_handoff_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

add_executable(death_recovery_test
  tools/death_recovery_test.cc
  src/dinput_native_proxy.cc
  src/native_d3d11_present_guard.cc
  "${CMAKE_CURRENT_BINARY_DIR}/native50_menu_assets.rc")
target_compile_features(death_recovery_test PRIVATE cxx_std_17)
target_compile_definitions(death_recovery_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(death_recovery_test PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(death_recovery_test PRIVATE d3d11 dxgi dxguid minhook winmm)

add_executable(player_wounds_test
  tools/player_wounds_test.cc
  src/dinput_native_proxy.cc
  src/native_d3d11_present_guard.cc)
target_compile_features(player_wounds_test PRIVATE cxx_std_17)
target_compile_definitions(player_wounds_test PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DIRECTINPUT_VERSION=0x0700
  DEATHTRAP_NATIVE_VERSION="${PROJECT_VERSION}")
target_compile_options(player_wounds_test PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(player_wounds_test PRIVATE d3d11 dxgi dxguid minhook winmm)

add_executable(model_pack_test tools/model_pack_test.cc)
target_compile_features(model_pack_test PRIVATE cxx_std_17)
target_compile_definitions(model_pack_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(model_pack_test PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(model_pack_test PRIVATE advapi32)

add_executable(music_redbook_probe
  tools/music_redbook_probe.cc)
target_compile_features(music_redbook_probe PRIVATE cxx_std_17)
target_compile_definitions(music_redbook_probe PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  UNICODE
  _UNICODE)
target_compile_options(music_redbook_probe PRIVATE
  /W4 /permissive- /EHsc /utf-8)
