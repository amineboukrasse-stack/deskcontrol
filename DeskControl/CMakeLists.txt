cmake_minimum_required(VERSION 3.16)
project(DeskControl VERSION 2.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(DESKCONTROL_BUILD_TESTS "Build the platform-independent unit tests" OFF)

# Platform independent logic: profiles, input state machine, persistence, keyboard layout.
set(DESKCONTROL_CORE_SRC src/model.cpp src/core.cpp src/store.cpp src/kbdlayout.cpp)

if(DESKCONTROL_BUILD_TESTS)
  enable_testing()
  add_executable(deskcontrol_tests tests/test_core.cpp ${DESKCONTROL_CORE_SRC})
  add_test(NAME core COMMAND deskcontrol_tests ${CMAKE_CURRENT_SOURCE_DIR}/Configs/config_default.ini)
endif()

if(WIN32)
  enable_language(RC)

  set(IMGUI_DIR third_party/imgui)
  add_executable(DeskControl WIN32
    src/main.cpp src/engine.cpp src/textfocus.cpp src/keyboard.cpp src/platform.cpp src/ui.cpp src/ui_pages.cpp
    ${DESKCONTROL_CORE_SRC}
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    res/deskcontrol.rc)

  target_include_directories(DeskControl PRIVATE src ${IMGUI_DIR} ${IMGUI_DIR}/backends)
  target_compile_definitions(DeskControl PRIVATE
    IMGUI_DEFINE_MATH_OPERATORS IMGUI_IMPL_WIN32_DISABLE_GAMEPAD NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(DeskControl PRIVATE d3d11 dxgi d3dcompiler dwmapi shell32 ole32 advapi32 user32 gdi32 comdlg32 winmm oleaut32 uuid)

  if(MSVC)
    target_compile_options(DeskControl PRIVATE /utf-8 /W3)
    # Static runtime: no "Visual C++ Redistributable" needed (the 1.x build required it).
    set_property(TARGET DeskControl PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_options(DeskControl PRIVATE /MANIFEST:NO)   # the manifest lives in res/deskcontrol.rc
  else()
    target_compile_options(DeskControl PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type)
    target_link_options(DeskControl PRIVATE -static -static-libgcc -static-libstdc++ -mwindows
      -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive)
  endif()
endif()
