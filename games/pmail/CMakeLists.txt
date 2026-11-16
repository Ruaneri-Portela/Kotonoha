cmake_minimum_required(VERSION 3.16)

project(Pmail VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
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
    src/PmailAudio.cpp
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
    add_executable(PmailPackBytesCheck tests/PackBytes.cpp)
    target_link_libraries(PmailPackBytesCheck PRIVATE PmailPort)
    add_test(NAME Pmail.PackBytes COMMAND PmailPackBytesCheck)
    add_executable(PmailComputerUiCheck tests/ComputerUi.cpp)
    target_link_libraries(PmailComputerUiCheck PRIVATE PmailPort)
    add_test(NAME Pmail.ComputerUi COMMAND PmailComputerUiCheck)
    add_executable(PmailWindowTintCheck tests/WindowTint.cpp)
    target_link_libraries(PmailWindowTintCheck PRIVATE PmailPort)
    add_test(NAME Pmail.WindowTint COMMAND PmailWindowTintCheck)
    add_executable(PmailStartupCheck tests/Startup.cpp)
    target_link_libraries(PmailStartupCheck PRIVATE PmailPort)
    add_test(NAME Pmail.Startup COMMAND PmailStartupCheck)
    add_executable(PmailAnimationFormatCheck tests/AnimationFormat.cpp)
    target_link_libraries(PmailAnimationFormatCheck PRIVATE Kotonoha::ProviderADV)
    add_test(NAME Pmail.AnimationFormat COMMAND PmailAnimationFormatCheck)
    # Build the route audit on all platforms, but run it only with retail
    # assets provided explicitly by the developer. Never run in default CI.
    add_executable(PmailRouteAuditCheck tests/RouteAudit.cpp)
    target_link_libraries(PmailRouteAuditCheck PRIVATE PmailPort)
    add_executable(PmailAudioCheck tests/AudioAssets.cpp)
    target_link_libraries(PmailAudioCheck PRIVATE Kotonoha::ProviderADV)
    target_compile_features(PmailAudioCheck PRIVATE cxx_std_17)
    add_executable(PmailGraphicsCheck tests/GraphicsAssets.cpp)
    target_link_libraries(PmailGraphicsCheck PRIVATE Kotonoha::ProviderADV)
    target_compile_features(PmailGraphicsCheck PRIVATE cxx_std_17)
    foreach(check IN ITEMS RuntimeAudio RuntimeBoot StoryFlow SaveState AnimationPlayback AnimationRender AudioCommands)
        set(check_target Pmail${check}Check)
        add_executable(${check_target} tests/${check}.cpp)
        target_link_libraries(${check_target} PRIVATE PmailPort)
    endforeach()
    set(PMAIL_TEST_ASSET_DIR "" CACHE PATH "Pure Mail DVD directory for retail integration tests")
    if(PMAIL_TEST_ASSET_DIR AND EXISTS "${PMAIL_TEST_ASSET_DIR}/data/pack/script.dat")
        foreach(check IN ITEMS Audio Graphics RuntimeAudio RuntimeBoot StoryFlow AnimationPlayback AnimationRender AudioCommands)
            add_test(NAME Pmail.${check}
                COMMAND Pmail${check}Check "${PMAIL_TEST_ASSET_DIR}")
            set_tests_properties(Pmail.${check} PROPERTIES
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
                TIMEOUT 180)
        endforeach()
        add_test(NAME Pmail.SaveState COMMAND PmailSaveStateCheck
            "${PMAIL_TEST_ASSET_DIR}/data/pack/script.dat")
        set_tests_properties(Pmail.SaveState PROPERTIES TIMEOUT 60)
        add_test(NAME Pmail.RouteAudit
            COMMAND PmailRouteAuditCheck "${PMAIL_TEST_ASSET_DIR}")
        set_tests_properties(Pmail.RouteAudit PROPERTIES TIMEOUT 120)
    endif()
endif()

kotonoha_label_directory_tests(REAL
    Pmail.Audio
    Pmail.Graphics
    Pmail.RuntimeAudio
    Pmail.RuntimeBoot
    Pmail.StoryFlow
    Pmail.AnimationPlayback
    Pmail.AnimationRender
    Pmail.AudioCommands
    Pmail.SaveState
    Pmail.RouteAudit
)
