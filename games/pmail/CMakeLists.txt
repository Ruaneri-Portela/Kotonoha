cmake_minimum_required(VERSION 3.16)

project(Pmail VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

# PMAIL VM and asset provider modules
if(NOT TARGET Kotonoha::Modules::VMs::Pmail)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/pmail"
        "${CMAKE_CURRENT_BINARY_DIR}/modules/vms/pmail")
endif()
if(NOT TARGET Kotonoha::ProviderPmail)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/pmail"
        "${CMAKE_CURRENT_BINARY_DIR}/modules/providers/pmail")
endif()
if(NOT TARGET Kotonoha::ProviderADV)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/adv"
        "${CMAKE_CURRENT_BINARY_DIR}/modules/providers/adv")
endif()

add_library(PmailPort STATIC
    src/PmailGame.cpp
    src/PmailRuntime.cpp
    src/PmailPack.cpp
    src/PmailSession.cpp
    src/PmailComponents.cpp
    src/PmailTitleMenu.cpp
    src/PmailStage.cpp
    src/PmailTextBox.cpp
    src/PmailPauseMenu.cpp
    src/PmailBacklog.cpp
)
target_include_directories(PmailPort PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(PmailPort PUBLIC
    Kotonoha::Engine
    Kotonoha::Modules::VMs::Pmail
    Kotonoha::ProviderPmail
    Kotonoha::ProviderADV
)
target_compile_features(PmailPort PUBLIC cxx_std_17)
set_target_properties(PmailPort PROPERTIES CXX_EXTENSIONS OFF)

kotonoha_add_app(Pmail src/main.cpp)
target_link_libraries(Pmail PRIVATE PmailPort)
set_target_properties(Pmail PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(Pmail
    BUNDLE_ID me.hirameki.kotonoha.pmail GAME pmail)

if(BUILD_TESTING)
    add_executable(PmailAudioCheck tests/AudioAssets.cpp)
    target_link_libraries(PmailAudioCheck PRIVATE Kotonoha::ProviderADV)
    target_compile_features(PmailAudioCheck PRIVATE cxx_std_17)
endif()
if(BUILD_TESTING)
    add_executable(PmailRuntimeAudioCheck tests/RuntimeAudio.cpp)
    target_link_libraries(PmailRuntimeAudioCheck PRIVATE PmailPort)
endif()
if(BUILD_TESTING)
    add_executable(PmailStoryFlowCheck tests/StoryFlow.cpp)
    target_link_libraries(PmailStoryFlowCheck PRIVATE PmailPort)
endif()
