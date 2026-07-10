# specgram lives in a sibling checkout on this machine rather than on a
# public remote, so the port fetches a pinned commit from that local
# repository. Bump REF when specs gains commits the app needs.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL file:///Users/jdf/hax/specs
    REF aae73bce080622abe05bc4bc8c2ab0df31c94c15
)

vcpkg_find_acquire_program(PKGCONFIG)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DSPECGRAM_BUILD_CLI=OFF
        "-DPKG_CONFIG_EXECUTABLE=${PKGCONFIG}"
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH share/specgram)

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
