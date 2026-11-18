# Shared PONscripter library. N2System may reuse the runtime without
# silently creating another game executable or package.
include_guard(GLOBAL)

if(NOT TARGET kotonoha_vm_ponscripter)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../modules/vms/ponscripter"
        "${CMAKE_CURRENT_BINARY_DIR}/vms/ponscripter")
endif()

if(NOT TARGET kotonoha_provider_ponscripter)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../modules/providers/ponscripter"
        "${CMAKE_CURRENT_BINARY_DIR}/providers/ponscripter")
endif()

if(NOT TARGET ponscripter_port)
    add_library(ponscripter_port STATIC
        "${CMAKE_CURRENT_LIST_DIR}/src/PonscripterRuntime.cpp"
    )
    target_include_directories(ponscripter_port PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/include")
    target_link_libraries(ponscripter_port PUBLIC
        KotonohaEngine
        kotonoha_provider_ponscripter
        kotonoha_vm_ponscripter
    )
    target_compile_features(ponscripter_port PUBLIC cxx_std_17)
    set_target_properties(ponscripter_port PROPERTIES CXX_EXTENSIONS OFF)
endif()
