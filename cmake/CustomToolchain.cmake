# CMakeLists.txt (Partial addition)

set(TOOLCHAIN_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/toolchain_install")
set(TOOLCHAIN_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/toolchain_build")

add_custom_command(
    OUTPUT "${TOOLCHAIN_INSTALL_DIR}/bin/i686-elf-gcc"
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/build_scripts/setup_toolchain.sh" "${TOOLCHAIN_INSTALL_DIR}" "${TOOLCHAIN_BUILD_DIR}"
    COMMENT "Building custom cross-toolchain"
    VERBATIM
)

add_custom_target(build_custom_toolchain DEPENDS "${TOOLCHAIN_INSTALL_DIR}/bin/i686-elf-gcc")

# Update cmake/toolchain.cmake to point to these binaries once built
# Ideally, you do this by setting cmake variables at the root level before adding subdirectories.
