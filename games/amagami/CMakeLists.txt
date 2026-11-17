cmake_minimum_required(VERSION 3.16)

project(Amagami VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

# Pure formats and disc source parsing do not depend on the VM.
# Session/Campaign are VM consumers and belong to the VM's sole archive.
add_library(AmagamiFormats STATIC
    src/Formats.cpp
    src/Source.cpp
)
target_include_directories(AmagamiFormats PUBLIC include)
target_link_libraries(AmagamiFormats PUBLIC KotonohaEngine)
target_compile_features(AmagamiFormats PUBLIC cxx_std_17)
set_target_properties(AmagamiFormats PROPERTIES CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/amagami/CMakeLists.txt")
target_sources(kotonoha_vm_amagami PRIVATE
    src/Session.cpp
    src/Campaign.cpp
)
target_include_directories(AmagamiFormats PUBLIC
    $<TARGET_PROPERTY:kotonoha_vm_amagami,INTERFACE_INCLUDE_DIRECTORIES>)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/amagami"
    "${CMAKE_CURRENT_BINARY_DIR}/providers/amagami")

add_library(amagami_port STATIC src/AmagamiRuntime.cpp src/AmagamiLauncher.cpp)
target_include_directories(amagami_port PUBLIC src)
target_link_libraries(amagami_port PUBLIC
    AmagamiFormats
    KotonohaEngine
    kotonoha_provider_amagami
    kotonoha_vm_amagami
)
set_target_properties(amagami_port PROPERTIES CXX_EXTENSIONS OFF)

kotonoha_add_app(Amagami src/main.cpp)
target_link_libraries(Amagami PRIVATE amagami_port)
set_target_properties(Amagami PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(Amagami
    BUNDLE_ID me.hirameki.kotonoha.amagami GAME amagami)

# Read-only Vita class loader and VM diagnostics use the existing SCF VM.
# Keep them separate from the PS2 runtime; no Vita gameplay is claimed.
if(KOTONOHA_BUILD_HOST_TOOLS)
    add_library(AmagamiVitaData STATIC
        src/VitaScripts.cpp
        src/VitaVmBridge.cpp
        src/VitaSpriteRenderer.cpp
        src/VitaInput.cpp
    )
    target_include_directories(AmagamiVitaData PUBLIC include)
    target_link_libraries(AmagamiVitaData PUBLIC
        AmagamiFormats kotonoha_provider_amagami kotonoha_vm_amagami)
    target_compile_features(AmagamiVitaData PUBLIC cxx_std_17)
    set_target_properties(AmagamiVitaData PROPERTIES CXX_EXTENSIONS OFF)
endif()

if(KOTONOHA_BUILD_HOST_TOOLS)
    add_executable(AmagamiInspect tools/Inspect.cpp)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AMAGAMI_JSON REQUIRED IMPORTED_TARGET json-c>=0.15)
    target_link_libraries(AmagamiInspect PRIVATE AmagamiFormats amagami_port kotonoha_vm_amagami PkgConfig::AMAGAMI_JSON)

    # Standalone Vita game-data inspector (execution remains PS2-only).
    add_executable(AmagamiVitaInspect tools/InspectVita.cpp)
    target_link_libraries(AmagamiVitaInspect PRIVATE AmagamiFormats kotonoha_provider_amagami)
    target_compile_features(AmagamiVitaInspect PRIVATE cxx_std_17)

    if(TARGET AmagamiVitaData)
        add_executable(AmagamiVitaProbe tools/VitaProbe.cpp)
        target_link_libraries(AmagamiVitaProbe PRIVATE AmagamiVitaData)
        target_compile_features(AmagamiVitaProbe PRIVATE cxx_std_17)
        add_executable(AmagamiVitaLive tools/VitaLive.cpp)
        target_link_libraries(AmagamiVitaLive PRIVATE AmagamiVitaData)
        target_compile_features(AmagamiVitaLive PRIVATE cxx_std_17)
    endif()
endif()

if(BUILD_TESTING)
add_subdirectory(tests)
endif()
