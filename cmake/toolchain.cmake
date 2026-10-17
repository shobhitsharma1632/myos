set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR i686)

# Toolchain installation directory (passed from superbuild or set as default)
if(NOT DEFINED TOOLCHAIN_INSTALL_DIR)
    set(TOOLCHAIN_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/toolchain_install")
endif()

# Specify the cross-compiler
set(CMAKE_C_COMPILER "${TOOLCHAIN_INSTALL_DIR}/bin/i686-elf-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_INSTALL_DIR}/bin/i686-elf-g++")
set(CMAKE_ASM_COMPILER nasm)

# Adjust flags for bare-metal
set(CMAKE_C_FLAGS "-ffreestanding -nostdlib -m32" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "-ffreestanding -nostdlib -m32 -fno-exceptions -fno-rtti" CACHE STRING "" FORCE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
