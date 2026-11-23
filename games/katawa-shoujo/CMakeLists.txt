cmake_minimum_required(VERSION 3.16)
project(KatawaShoujo VERSION 0.1.0 LANGUAGES CXX)

if(NOT TARGET doki_doki_literature_club)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../doki-doki-literature-club"
        "${CMAKE_CURRENT_BINARY_DIR}/renpy-shared")
endif()

kotonoha_add_app(KatawaShoujo src/main.cpp)
target_link_libraries(KatawaShoujo PRIVATE doki_doki_literature_club)
set_target_properties(KatawaShoujo PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(KatawaShoujo
    BUNDLE_ID me.hirameki.kotonoha.katawashoujo GAME katawa-shoujo
    PYTHON_STDLIB "${KOTONOHA_RENPY_PYTHON_STDLIB}"
    PYTHON_VERSION "${KOTONOHA_RENPY_PYTHON_VERSION}")

set(KATAWA_DATA_DIR "" CACHE PATH "Katawa Shoujo retail game directory")
if(BUILD_TESTING AND NOT ANDROID)
    add_executable(KatawaIdentityTest tests/KatawaIdentityTest.cpp)
    target_link_libraries(KatawaIdentityTest PRIVATE doki_doki_literature_club)
    add_executable(KatawaStoryTest tests/KatawaStoryTest.cpp)
    target_link_libraries(KatawaStoryTest PRIVATE doki_doki_literature_club)
    if(KATAWA_DATA_DIR)
        add_test(NAME Katawa.Identity COMMAND KatawaIdentityTest "${KATAWA_DATA_DIR}")
        add_test(NAME Katawa.Story COMMAND KatawaStoryTest "${KATAWA_DATA_DIR}")
        set_tests_properties(Katawa.Story PROPERTIES TIMEOUT 120)
    endif()
endif()
