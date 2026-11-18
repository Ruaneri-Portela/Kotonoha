# C++17 PONscripter / ONScripter VM and SAR/NSA archive reader (zero SDL dependency).
if(TARGET kotonoha_vm_ponscripter)
    return()
endif()

add_library(kotonoha_vm_ponscripter STATIC
    src/Archive.cpp
    src/PonscripterTypes.cpp
    src/ScriptBuffer.cpp
    src/VariableStore.cpp
    src/Lexer.cpp
    src/TextFormatter.cpp
    src/SaveSerializer.cpp
    src/PonscripterVm.cpp
    src/PonscripterCommands.cpp
)

target_include_directories(kotonoha_vm_ponscripter PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include"
)
target_compile_features(kotonoha_vm_ponscripter PUBLIC cxx_std_17)
set_target_properties(kotonoha_vm_ponscripter PROPERTIES CXX_EXTENSIONS OFF)

if(BUILD_TESTING AND NOT ANDROID AND NOT EMSCRIPTEN)
    add_executable(KotonohaPonscripterLexerAndExprTests tests/LexerAndExprTest.cpp)
    target_link_libraries(KotonohaPonscripterLexerAndExprTests PRIVATE kotonoha_vm_ponscripter)
    add_test(NAME Kotonoha.Ponscripter.LexerAndExpr COMMAND KotonohaPonscripterLexerAndExprTests)
    set_tests_properties(Kotonoha.Ponscripter.LexerAndExpr PROPERTIES LABELS synthetic)

    add_executable(KotonohaPonscripterTaggedStringTests tests/TaggedStringTest.cpp)
    target_link_libraries(KotonohaPonscripterTaggedStringTests PRIVATE kotonoha_vm_ponscripter)
    add_test(NAME Kotonoha.Ponscripter.TaggedString COMMAND KotonohaPonscripterTaggedStringTests)
    set_tests_properties(Kotonoha.Ponscripter.TaggedString PROPERTIES LABELS synthetic)

    add_executable(KotonohaPonscripterVmFlowAndMenuTests tests/VmFlowAndMenuTest.cpp)
    target_link_libraries(KotonohaPonscripterVmFlowAndMenuTests PRIVATE kotonoha_vm_ponscripter)
    add_test(NAME Kotonoha.Ponscripter.VmFlowAndMenu COMMAND KotonohaPonscripterVmFlowAndMenuTests)
    set_tests_properties(Kotonoha.Ponscripter.VmFlowAndMenu PROPERTIES LABELS synthetic)
endif()
