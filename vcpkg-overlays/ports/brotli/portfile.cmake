vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO google/brotli
    REF v${VERSION}
    SHA512 460591287d428c8b5a8a2766b5b424d14501fcda12c7118b8f3cc4bfae44b90ac75ca156557f07b6e22542612724d904533e5f70019be07dbbb604c46f03ab69
    HEAD_REF master
    PATCHES
        install.patch
        pkgconfig.patch
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBROTLI_DISABLE_TESTS=ON
        # O CLI brotli Aciona wasm-opt -O3 no Emscripten e trava; so a
        # biblioteca e necessaria para o Kotonoha.
        -DBROTLI_BUILD_TOOLS=OFF
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()
vcpkg_cmake_config_fixup(CONFIG_PATH share/unofficial-brotli PACKAGE_NAME unofficial-brotli)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/tools")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/man")

# CLI desabilitado no overlay: nao ha ferramenta para copiar.

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
