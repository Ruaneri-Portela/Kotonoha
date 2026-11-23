# Summer Radish Machine virtual machine. Reads SummerRadish/Pack.hpp, provided
# by kotonoha_provider_cmp; the format library itself does not link the VM.
if(TARGET kotonoha_vm_summer-radish)
    return()
endif()

add_library(kotonoha_vm_summer-radish STATIC
    "${CMAKE_CURRENT_LIST_DIR}/src/Script.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/ImoutoScript.cpp"
)

target_include_directories(kotonoha_vm_summer-radish PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(kotonoha_vm_summer-radish PUBLIC KotonohaEngine PRIVATE kotonoha_provider_cmp)
target_compile_features(kotonoha_vm_summer-radish PUBLIC cxx_std_17)
set_target_properties(kotonoha_vm_summer-radish PROPERTIES CXX_EXTENSIONS OFF)
