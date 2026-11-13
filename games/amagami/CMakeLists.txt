cmake_minimum_required(VERSION 3.16)

project(Amagami VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

add_library(AmagamiFormats STATIC
    src/Formats.cpp
    src/Source.cpp
    src/Session.cpp
    src/Campaign.cpp
)
target_include_directories(AmagamiFormats PUBLIC include)
target_link_libraries(AmagamiFormats PUBLIC Kotonoha::Engine Kotonoha::Vmamagami)
target_compile_features(AmagamiFormats PUBLIC cxx_std_17)
set_target_properties(AmagamiFormats PROPERTIES CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/amagami/CMakeLists.txt")
target_include_directories(AmagamiFormats PUBLIC
    $<TARGET_PROPERTY:kotonoha_vm_amagami,INTERFACE_INCLUDE_DIRECTORIES>)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/amagami"
    "${CMAKE_CURRENT_BINARY_DIR}/providers/amagami")

add_library(amagami_port STATIC src/AmagamiRuntime.cpp src/AmagamiLauncher.cpp)
target_include_directories(amagami_port PUBLIC src)
target_link_libraries(amagami_port PUBLIC
    AmagamiFormats
    Kotonoha::Engine
    Kotonoha::ProviderAmagami
    Kotonoha::Vmamagami
)
set_target_properties(amagami_port PROPERTIES CXX_EXTENSIONS OFF)

kotonoha_add_app(Amagami src/main.cpp)
target_link_libraries(Amagami PRIVATE amagami_port)
set_target_properties(Amagami PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(Amagami
    BUNDLE_ID me.hirameki.kotonoha.amagami GAME amagami)

if(NOT ANDROID AND NOT EMSCRIPTEN)
    add_executable(AmagamiInspect tools/Inspect.cpp)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AMAGAMI_JSON REQUIRED IMPORTED_TARGET json-c>=0.15)
    target_link_libraries(AmagamiInspect PRIVATE AmagamiFormats amagami_port Kotonoha::Vmamagami PkgConfig::AMAGAMI_JSON)
endif()

if(BUILD_TESTING)
add_subdirectory(tests)
endif()
