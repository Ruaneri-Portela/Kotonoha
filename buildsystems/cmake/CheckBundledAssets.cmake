if(EXISTS "${GAME_ASSET_DIR}")
    file(GLOB contents "${GAME_ASSET_DIR}/*" "${GAME_ASSET_DIR}/.[!.]*")
    if(contents)
        message(FATAL_ERROR
            "The bundle contains previously copied game assets: ${GAME_ASSET_DIR}. "
            "Clean the build before distributing it or enable KOTONOHA_BUNDLE_LOCAL_GAME_ASSETS.")
    endif()
endif()
