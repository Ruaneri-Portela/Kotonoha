# C++17 NsScript indexing foundation and N2System motor VM.
if(TARGET kotonoha_vm_n2system)
    return()
endif()
add_library(kotonoha_vm_n2system STATIC
    src/ScriptIndex.cpp
    src/DebugCatalog.cpp
    src/ScriptExecutor.cpp
    src/VariableStore.cpp
    src/SceneGraph.cpp
    src/AudioSystem.cpp
    src/DialogueSystem.cpp
    src/SaveState.cpp
    src/SaveStateJson.cpp
    src/ScriptVm.cpp
    src/NsbScript.cpp
    src/NsbVm.cpp
    src/NsbSnapshot.cpp
)
target_include_directories(kotonoha_vm_n2system PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_features(kotonoha_vm_n2system PUBLIC cxx_std_17)
set_target_properties(kotonoha_vm_n2system PROPERTIES CXX_EXTENSIONS OFF)
if(BUILD_TESTING AND NOT ANDROID AND NOT EMSCRIPTEN)
    add_executable(KotonohaN2SystemDebugCatalogTests tests/DebugCatalogTest.cpp)
    target_link_libraries(KotonohaN2SystemDebugCatalogTests PRIVATE kotonoha_vm_n2system)
    add_test(NAME Kotonoha.N2System.DebugCatalog COMMAND KotonohaN2SystemDebugCatalogTests)
    set_tests_properties(Kotonoha.N2System.DebugCatalog PROPERTIES LABELS synthetic)
    add_executable(KotonohaN2SystemScriptTests tests/ScriptIndexTest.cpp)
    target_link_libraries(KotonohaN2SystemScriptTests PRIVATE kotonoha_vm_n2system)
    add_test(NAME Kotonoha.N2System.ScriptIndex COMMAND KotonohaN2SystemScriptTests)
    set_tests_properties(Kotonoha.N2System.ScriptIndex PROPERTIES LABELS synthetic)
    add_executable(KotonohaN2SystemExecutorTests tests/ScriptExecutorTest.cpp)
    target_link_libraries(KotonohaN2SystemExecutorTests PRIVATE kotonoha_vm_n2system)
    add_test(NAME Kotonoha.N2System.Executor COMMAND KotonohaN2SystemExecutorTests)
    set_tests_properties(Kotonoha.N2System.Executor PROPERTIES LABELS synthetic)
    add_executable(KotonohaN2SystemVmTests tests/ScriptVmTest.cpp)
    target_link_libraries(KotonohaN2SystemVmTests PRIVATE kotonoha_vm_n2system)
    add_test(NAME Kotonoha.N2System.ScriptVm COMMAND KotonohaN2SystemVmTests)
    set_tests_properties(Kotonoha.N2System.ScriptVm PROPERTIES LABELS synthetic)
    add_executable(KotonohaN2SystemEntityQueryTests tests/EntityQueryTest.cpp)
    target_link_libraries(KotonohaN2SystemEntityQueryTests PRIVATE kotonoha_vm_n2system)
    add_test(NAME Kotonoha.N2System.EntityQuery COMMAND KotonohaN2SystemEntityQueryTests)
    set_tests_properties(Kotonoha.N2System.EntityQuery PROPERTIES LABELS synthetic)
    add_executable(KotonohaN2SystemAnimationTimingTests tests/AnimationTimingTest.cpp)
    target_link_libraries(KotonohaN2SystemAnimationTimingTests PRIVATE kotonoha_vm_n2system)
    add_test(NAME Kotonoha.N2System.AnimationTiming COMMAND KotonohaN2SystemAnimationTimingTests)
    set_tests_properties(Kotonoha.N2System.AnimationTiming PROPERTIES LABELS synthetic)
    add_executable(KotonohaN2SystemImageEffectsTests tests/ImageEffectsTest.cpp)
    target_link_libraries(KotonohaN2SystemImageEffectsTests PRIVATE kotonoha_vm_n2system)
    add_test(NAME Kotonoha.N2System.ImageEffects COMMAND KotonohaN2SystemImageEffectsTests)
    set_tests_properties(Kotonoha.N2System.ImageEffects PROPERTIES LABELS synthetic)
    add_executable(KotonohaN2SystemNsbVmTests tests/NsbVmTest.cpp)
    target_link_libraries(KotonohaN2SystemNsbVmTests PRIVATE kotonoha_vm_n2system)
    add_test(NAME Kotonoha.N2System.NsbVm COMMAND KotonohaN2SystemNsbVmTests)
    set_tests_properties(Kotonoha.N2System.NsbVm PROPERTIES LABELS synthetic)
endif()
