# Shared Artemis Engine runtime library for standalone and unified launcher builds.
include_guard(GLOBAL)

if(NOT TARGET kotonoha_vm_artemis)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../modules/vms/artemis"
        "${CMAKE_CURRENT_BINARY_DIR}/vms/artemis")
endif()

if(NOT TARGET kotonoha_provider_artemis)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../modules/providers/artemis"
        "${CMAKE_CURRENT_BINARY_DIR}/providers/artemis")
endif()

if(NOT TARGET artemis_port)
    add_library(artemis_port STATIC
        "${CMAKE_CURRENT_LIST_DIR}/src/ArtemisRuntime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/ArtemisUi.cpp"
    )
    target_include_directories(artemis_port PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/include"
    )
    target_link_libraries(artemis_port PUBLIC
        KotonohaEngine
        kotonoha_provider_artemis
        kotonoha_vm_artemis
    )
    target_compile_features(artemis_port PUBLIC cxx_std_17)
    set_target_properties(artemis_port PROPERTIES CXX_EXTENSIONS OFF)
endif()
