# Kirikiri KAG Runner virtual machine. Only depends on Kotonoha::Engine; the
# kirikiri formats/runtime links Kotonoha::Modules::VMs::KAG directly.
if(TARGET Kotonoha::Modules::VMs::KAG)
    return()
endif()

add_library(kotonoha_vm_kag STATIC "${CMAKE_CURRENT_LIST_DIR}/src/KagScript.cpp")
add_library(Kotonoha::Modules::VMs::KAG ALIAS kotonoha_vm_kag)
add_library(Kotonoha::Vmkag ALIAS kotonoha_vm_kag)

target_include_directories(kotonoha_vm_kag PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(kotonoha_vm_kag PUBLIC Kotonoha::Engine)
target_compile_features(kotonoha_vm_kag PUBLIC cxx_std_17)
set_target_properties(kotonoha_vm_kag PROPERTIES CXX_EXTENSIONS OFF)
