# Dear ImGui comes from exactly two providers: the pinned Git submodule or
# vcpkg (with its SDL3/SDL3 renderer backends). SYSTEM packages are never
# searched: there is no find_package(imgui)/pkg-config imgui fallback here.
# KOTONOHA_IMGUI_PROVIDER=SYSTEM is rejected by KotonohaDepsProvider.cmake.
include("${CMAKE_CURRENT_LIST_DIR}/KotonohaDepsProvider.cmake")

if(DEFINED KOTONOHA_IMGUI_PROVIDER_EFFECTIVE AND
   NOT KOTONOHA_IMGUI_PROVIDER_EFFECTIVE STREQUAL "")
    set(_kotonoha_imgui_request "${KOTONOHA_IMGUI_PROVIDER_EFFECTIVE}")
else()
    set(_kotonoha_imgui_request "${KOTONOHA_IMGUI_PROVIDER}")
endif()
string(TOUPPER "${_kotonoha_imgui_request}" _kotonoha_imgui_request)
if(_kotonoha_imgui_request STREQUAL "SYSTEM")
    message(FATAL_ERROR
        "[Kotonoha] Dear ImGui cannot be resolved from SYSTEM packages. "
        "Use KOTONOHA_IMGUI_PROVIDER=SUBMODULES or "
        "KOTONOHA_IMGUI_PROVIDER=VCPKG instead.")
endif()
if(NOT _kotonoha_imgui_request MATCHES "^(AUTO|SUBMODULES|VCPKG)$")
    message(FATAL_ERROR
        "[Kotonoha] Invalid ImGui provider: ${_kotonoha_imgui_request}. "
        "Expected AUTO, SUBMODULES or VCPKG.")
endif()
# Android requires the checked-out submodule (SDL Java integration and the
# per-ABI vcpkg tree does not install an imgui package).
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(_kotonoha_imgui_request SUBMODULES)
endif()

set(_kotonoha_imgui_resolved SUBMODULES)
set(_kotonoha_imgui_config OFF)
if(_kotonoha_imgui_request STREQUAL "VCPKG")
    if(NOT VCPKG_INSTALLED_DIR OR NOT VCPKG_TARGET_TRIPLET)
        message(FATAL_ERROR
            "[Kotonoha] Dear ImGui from vcpkg requires the vcpkg toolchain "
            "on the FIRST configure and the \"imgui-vcpkg\" manifest feature "
            "(which enables imgui's sdl3-binding and sdl3-renderer-binding). "
            "Use KOTONOHA_IMGUI_PROVIDER=SUBMODULES for a pinned submodule "
            "build instead.")
    endif()
    # Restrict the search to the selected vcpkg triplet so a system imgui
    # installation can never be mixed into this build.
    set(_kotonoha_imgui_prefix
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    find_package(imgui CONFIG QUIET
        PATHS "${_kotonoha_imgui_prefix}" NO_DEFAULT_PATH)
    if(NOT TARGET imgui::imgui)
        message(FATAL_ERROR
            "[Kotonoha] Dear ImGui was not found in "
            "${_kotonoha_imgui_prefix}. Add \"imgui-vcpkg\" to "
            "VCPKG_MANIFEST_FEATURES, or use "
            "KOTONOHA_IMGUI_PROVIDER=SUBMODULES.")
    endif()
    # Keep the historical `imgui` target name used across engine, tests and
    # game ports. The imported imgui::imgui target is re-discovered in the
    # root CMakeLists.txt for sibling directories.
    set(_kotonoha_imgui_type INTERFACE)
    set(_kotonoha_imgui_sources "")
    set(_kotonoha_imgui_resolved VCPKG)
    set(_kotonoha_imgui_config ON)
else()
    include("${CMAKE_CURRENT_LIST_DIR}/KotonohaGitSubmodules.cmake")
    kotonoha_require_submodule("lib/submodules/imgui" "backends/imgui_impl_sdl3.cpp")
    foreach(file IN ITEMS imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
        backends/imgui_impl_sdl3.cpp backends/imgui_impl_sdlrenderer3.cpp
        misc/cpp/imgui_stdlib.cpp)
      if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/submodules/imgui/${file}")
        message(FATAL_ERROR "[Kotonoha] Incomplete pinned ImGui submodule: ${file}")
      endif()
    endforeach()
    set(_kotonoha_imgui_type ${KOTONOHA_LIB_TYPE})
    set(_kotonoha_imgui_sources
      submodules/imgui/imgui.cpp
      submodules/imgui/imgui_draw.cpp
      submodules/imgui/imgui_tables.cpp
      submodules/imgui/imgui_widgets.cpp
      submodules/imgui/backends/imgui_impl_sdl3.cpp
      submodules/imgui/backends/imgui_impl_sdlrenderer3.cpp
      submodules/imgui/misc/cpp/imgui_stdlib.cpp)
endif()

# One canonical declaration, independent of the selected provider.
if(NOT TARGET imgui)
    add_library(imgui ${_kotonoha_imgui_type} ${_kotonoha_imgui_sources})
endif()
if(_kotonoha_imgui_config)
    target_link_libraries(imgui INTERFACE imgui::imgui)
else()
    target_include_directories(imgui PUBLIC
      submodules/imgui submodules/imgui/backends submodules/imgui/misc/cpp)
    target_link_libraries(imgui PUBLIC SDL3::SDL3)
    set_target_properties(imgui PROPERTIES
      CXX_EXTENSIONS OFF
      WINDOWS_EXPORT_ALL_SYMBOLS ON
      OUTPUT_NAME Imgui)
    if(WIN32)
      set_target_properties(imgui PROPERTIES
        PREFIX "" IMPORT_PREFIX "" IMPORT_SUFFIX ".lib")
    endif()
endif()

# The cache values are INTERNAL: the user-facing option is
# KOTONOHA_IMGUI_PROVIDER, declared in KotonohaDepsProvider.cmake.
set(KOTONOHA_IMGUI_TARGET imgui CACHE INTERNAL "Shared ImGui target" FORCE)
set(KOTONOHA_IMGUI_PROVIDER_RESOLVED "${_kotonoha_imgui_resolved}" CACHE INTERNAL
    "Resolved Dear ImGui provider (SUBMODULES or VCPKG)" FORCE)
set(KOTONOHA_IMGUI_CONFIG "${_kotonoha_imgui_config}" CACHE INTERNAL "" FORCE)
message(STATUS "[Kotonoha] Dear ImGui provider: ${_kotonoha_imgui_resolved}")
