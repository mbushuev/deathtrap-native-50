cmake_minimum_required(VERSION 3.21)
project(deathtrap_native50_overlay VERSION 0.0.184 LANGUAGES C CXX)

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

add_library(deathtrap_native_dinput SHARED
  src/deathtrap_native_render_patch.cc
  src/dinput_native_proxy.cc
  src/dinput_native_proxy.def
  src/deathtrap_native_version.rc
  src/native_d3d11_present_guard.cc
)
target_compile_features(deathtrap_native_dinput PRIVATE cxx_std_17)
target_compile_definitions(deathtrap_native_dinput PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  UNICODE
  _UNICODE
  DIRECTINPUT_VERSION=0x0700
)
target_compile_options(deathtrap_native_dinput PRIVATE
  /W4 /permissive- /EHsc /utf-8)
target_link_options(deathtrap_native_dinput PRIVATE
  /IGNORE:4222)
target_link_libraries(deathtrap_native_dinput PRIVATE
  d3d11 dxgi dxguid minhook)
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
