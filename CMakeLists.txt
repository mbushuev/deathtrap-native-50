cmake_minimum_required(VERSION 3.21)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" DEATHTRAP_NATIVE_VERSION
  LIMIT_COUNT 1)
string(STRIP "${DEATHTRAP_NATIVE_VERSION}" DEATHTRAP_NATIVE_VERSION)
if(NOT DEATHTRAP_NATIVE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must contain a semantic version such as 0.0.0")
endif()

project(deathtrap_native50_overlay VERSION ${DEATHTRAP_NATIVE_VERSION}
  LANGUAGES C CXX)

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

add_library(deathtrap_native_dinput SHARED
  src/deathtrap_native_render_patch.cc
  src/dinput_native_proxy.cc
  src/dinput_native_proxy.def
  "${CMAKE_CURRENT_BINARY_DIR}/deathtrap_native_version.rc"
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
target_link_libraries(dinput_proxy_smoke_test PRIVATE dxguid)

add_executable(camera_spring_arm_test
  tools/camera_spring_arm_test.cc)
target_compile_features(camera_spring_arm_test PRIVATE cxx_std_17)
target_include_directories(camera_spring_arm_test PRIVATE src)
target_compile_options(camera_spring_arm_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

add_executable(camera_room_collision_test
  tools/camera_room_collision_test.cc)
target_compile_features(camera_room_collision_test PRIVATE cxx_std_17)
target_include_directories(camera_room_collision_test PRIVATE src)
target_compile_options(camera_room_collision_test PRIVATE
  /W4 /permissive- /EHsc /utf-8)

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
