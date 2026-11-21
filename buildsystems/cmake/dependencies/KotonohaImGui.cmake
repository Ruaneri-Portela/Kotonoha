# Build Dear ImGui core and SDL3 backends from the same pinned Git submodule.
include("${CMAKE_CURRENT_LIST_DIR}/KotonohaGitSubmodules.cmake")
set(KOTONOHA_IMGUI_PROVIDER SUBMODULES CACHE STRING "Pinned ImGui submodule" FORCE)
set_property(CACHE KOTONOHA_IMGUI_PROVIDER PROPERTY STRINGS SUBMODULES)
kotonoha_require_submodule("lib/submodules/imgui" "backends/imgui_impl_sdl3.cpp")
foreach(file IN ITEMS imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
    backends/imgui_impl_sdl3.cpp backends/imgui_impl_sdlrenderer3.cpp
    misc/cpp/imgui_stdlib.cpp)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/submodules/imgui/${file}")
    message(FATAL_ERROR "[Kotonoha] Incomplete pinned ImGui submodule: ${file}")
  endif()
endforeach()
add_library(imgui ${KOTONOHA_LIB_TYPE}
  submodules/imgui/imgui.cpp
  submodules/imgui/imgui_draw.cpp
  submodules/imgui/imgui_tables.cpp
  submodules/imgui/imgui_widgets.cpp
  submodules/imgui/backends/imgui_impl_sdl3.cpp
  submodules/imgui/backends/imgui_impl_sdlrenderer3.cpp
  submodules/imgui/misc/cpp/imgui_stdlib.cpp)
target_include_directories(imgui PUBLIC
  submodules/imgui submodules/imgui/backends submodules/imgui/misc/cpp)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
set_target_properties(imgui PROPERTIES CXX_EXTENSIONS OFF WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(KOTONOHA_IMGUI_TARGET imgui CACHE INTERNAL "Shared ImGui target" FORCE)
set(KOTONOHA_IMGUI_PROVIDER_RESOLVED SUBMODULES CACHE INTERNAL "" FORCE)
set(KOTONOHA_IMGUI_CONFIG OFF CACHE INTERNAL "" FORCE)
message(STATUS "[Kotonoha] ImGui provider: SUBMODULES (SDL3 backends)")
