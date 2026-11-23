add_executable(SummerFormatsTest FormatsTest.cpp GraphicsTest.cpp MapTest.cpp)
target_link_libraries(SummerFormatsTest PRIVATE SummerRadishFormats)
add_test(NAME SummerRadish.Formats COMMAND SummerFormatsTest)
set(SUMMER_DATA_DIR "" CACHE PATH "Summer Radish retail disc root or PACK directory")
if(SUMMER_DATA_DIR AND NOT ANDROID)
    add_test(NAME SummerRadish.Retail COMMAND SummerInspect "${SUMMER_DATA_DIR}" --validate)
    set_tests_properties(SummerRadish.Retail PROPERTIES TIMEOUT 120)
    add_test(NAME SummerRadish.Boot COMMAND SummerRadish --summer "${SUMMER_DATA_DIR}" --no-config --no-launcher --frames 120)
    set_tests_properties(SummerRadish.Boot PROPERTIES TIMEOUT 120 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
endif()
if(NOT ANDROID)
    add_executable(SummerMoviePathTest MoviePathTest.cpp)
    target_link_libraries(SummerMoviePathTest PRIVATE kotonoha_provider_cmp)
    add_executable(SummerEngineTest EngineTest.cpp)
    target_link_libraries(SummerEngineTest PRIVATE summer_radish_vacation)
    add_executable(SummerRouteTest RouteTest.cpp)
    target_link_libraries(SummerRouteTest PRIVATE SummerRadishFormats)
    if(SUMMER_DATA_DIR)
        add_test(NAME SummerRadish.MoviePath COMMAND SummerMoviePathTest "${SUMMER_DATA_DIR}")
        add_test(NAME SummerRadish.Engine COMMAND SummerEngineTest "${SUMMER_DATA_DIR}")
        set_tests_properties(SummerRadish.Engine PROPERTIES TIMEOUT 120 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
        add_test(NAME SummerRadish.Routes COMMAND SummerRouteTest "${SUMMER_DATA_DIR}")
        set_tests_properties(SummerRadish.Routes PROPERTIES TIMEOUT 120)
    endif()
endif()

# LostM and Imouto de Ikou ship as two discs. The single SUMMER_DATA_DIR gate
# cannot describe them: DISC1 lacks VOICE.CMP/BGMDATA.CMP, which live on DISC2.
# Point one gate at each real mount point; SummerAssets combines both discs in
# a layered read-only VFS. Tests return 77 when the gates are absent so CTest
# reports them as skipped rather than failing.
set(LOSTM_DISC1_DIR "" CACHE PATH "Lost M DISC1 directory (retail)")
set(LOSTM_DISC2_DIR "" CACHE PATH "Lost M DISC2 directory (retail)")
set(IMOUTO_DISC1_DIR "" CACHE PATH "Imouto de Ikou DISC1 directory (retail)")
set(IMOUTO_DISC2_DIR "" CACHE PATH "Imouto de Ikou DISC2 directory (retail)")

function(summer_disc_paths out disc1 disc2)
    if(disc1 AND disc2)
        set(${out} "${disc1}" "${disc2}" PARENT_SCOPE)
    else()
        set(${out} "" PARENT_SCOPE)
    endif()
endfunction()

if(NOT ANDROID)
    summer_disc_paths(lostmPaths "${LOSTM_DISC1_DIR}" "${LOSTM_DISC2_DIR}")
    add_test(NAME LostM.Retail COMMAND SummerInspect ${lostmPaths} --validate)
    add_test(NAME LostM.Engine COMMAND SummerEngineTest ${lostmPaths})
    set_tests_properties(LostM.Retail LostM.Engine PROPERTIES
        LABELS real SKIP_RETURN_CODE 77 TIMEOUT 120
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

    summer_disc_paths(imoutoPaths "${IMOUTO_DISC1_DIR}" "${IMOUTO_DISC2_DIR}")
    add_test(NAME Imouto.Engine COMMAND SummerEngineTest ${imoutoPaths})
    set_tests_properties(Imouto.Engine PROPERTIES
        LABELS real SKIP_RETURN_CODE 77 TIMEOUT 120
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")

    # The app itself is only built for the summer port; run it through the
    # same layered mount used by the tests to cover the CLI/boot path.
    if(TARGET SummerRadish)
        if(LOSTM_DISC1_DIR AND LOSTM_DISC2_DIR)
            add_test(NAME LostM.Boot COMMAND SummerRadish
                --summer "${LOSTM_DISC1_DIR}"
                --summer-disc2 "${LOSTM_DISC2_DIR}"
                --no-config --no-launcher --frames 120)
            set_tests_properties(LostM.Boot PROPERTIES
                LABELS real TIMEOUT 120
                ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
        endif()
        if(IMOUTO_DISC1_DIR AND IMOUTO_DISC2_DIR)
            add_test(NAME Imouto.Boot COMMAND SummerRadish
                --summer "${IMOUTO_DISC1_DIR}"
                --summer-disc2 "${IMOUTO_DISC2_DIR}"
                --no-config --no-launcher --frames 120)
            set_tests_properties(Imouto.Boot PROPERTIES
                LABELS real TIMEOUT 120
                ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
        endif()
    endif()
endif()

kotonoha_label_directory_tests(REAL
    SummerRadish.Retail
    SummerRadish.Boot
    SummerRadish.MoviePath
    SummerRadish.Engine
    SummerRadish.Routes
    LostM.Retail
    LostM.Engine
    LostM.Boot
    Imouto.Engine
    Imouto.Boot
)
