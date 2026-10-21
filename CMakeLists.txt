cmake_minimum_required(VERSION 3.10)
project(myos C CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Source directories
set(OS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Subdirectories
add_subdirectory(src/bootloader/stage1)
add_subdirectory(src/bootloader/stage2)
add_subdirectory(src/kernel)
add_subdirectory(src/user)

# Image creation
find_program(MKFS_FAT_EXECUTABLE NAMES mkfs.fat mkfs.vfat)
find_program(MCOPY_EXECUTABLE NAMES mcopy)
find_program(MMD_EXECUTABLE NAMES mmd)
find_program(DD_EXECUTABLE NAMES dd)
find_program(PYTHON3_EXECUTABLE NAMES python3 python)

if(MKFS_FAT_EXECUTABLE AND MCOPY_EXECUTABLE AND MMD_EXECUTABLE AND DD_EXECUTABLE AND PYTHON3_EXECUTABLE)
    message(STATUS "Found image creation tools: mkfs.fat, mcopy, mmd, dd, python3")

    set(FLOPPY_IMAGE "${CMAKE_CURRENT_BINARY_DIR}/main_floppy.img")

    set(BOOTLOADER_STAGE1_BIN "${CMAKE_CURRENT_BINARY_DIR}/src/bootloader/stage1/boot.bin")
    set(BOOTLOADER_STAGE1_MAP "${CMAKE_CURRENT_BINARY_DIR}/src/bootloader/stage1/boot.map")
    set(BOOTLOADER_STAGE2_BIN "${CMAKE_CURRENT_BINARY_DIR}/src/bootloader/stage2/stage2.bin")
    set(KERNEL_ELF "${CMAKE_CURRENT_BINARY_DIR}/src/kernel/kernel.elf")
    set(INSTALL_BOOTLOADER_PY "${CMAKE_CURRENT_SOURCE_DIR}/build_scripts/install_bootloader.py")

    add_custom_target(floppy_image ALL
        COMMAND ${DD_EXECUTABLE} if=/dev/zero of=${FLOPPY_IMAGE} bs=512 count=2880
        COMMAND ${MKFS_FAT_EXECUTABLE} -F 12 -R 33 -n "NBOS" ${FLOPPY_IMAGE}
        COMMAND ${DD_EXECUTABLE} if=${BOOTLOADER_STAGE2_BIN} of=${FLOPPY_IMAGE} bs=512 seek=1 conv=notrunc
        COMMAND ${MMD_EXECUTABLE} -i ${FLOPPY_IMAGE} "::boot"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} ${KERNEL_ELF} "::boot/kernel.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_SOURCE_DIR}/disk_files/readme.txt" "::readme.txt"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_SOURCE_DIR}/disk_files/hello.txt" "::hello.txt"
        COMMAND ${MMD_EXECUTABLE} -i ${FLOPPY_IMAGE} "::bin"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/hello.elf" "::bin/hello.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/calc.elf" "::bin/calc.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/counter.elf" "::bin/counter.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/cat.elf" "::bin/cat.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/wc.elf" "::bin/wc.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/grep.elf" "::bin/grep.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/sh.elf" "::bin/sh.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/snake.elf" "::bin/snake.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/ifconfig.elf" "::bin/ifconfig.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/ping.elf" "::bin/ping.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/tee.elf" "::bin/tee.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/head.elf" "::bin/head.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/curl.elf" "::bin/curl.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/httpd.elf" "::bin/httpd.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/alarm.elf" "::bin/alarm.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/trap.elf" "::bin/trap.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/notepad.elf" "::bin/notepad.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/sysmon.elf" "::bin/sysmon.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/paint.elf" "::bin/paint.elf"
        COMMAND ${MCOPY_EXECUTABLE} -i ${FLOPPY_IMAGE} "${CMAKE_CURRENT_BINARY_DIR}/src/user/smpinfo.elf" "::bin/smpinfo.elf"
        COMMAND ${PYTHON3_EXECUTABLE} ${INSTALL_BOOTLOADER_PY} ${FLOPPY_IMAGE} ${BOOTLOADER_STAGE1_BIN} ${BOOTLOADER_STAGE1_MAP} ${BOOTLOADER_STAGE2_BIN} 0
        DEPENDS bootloader_stage1 bootloader_stage2 kernel user_apps
        COMMENT "Creating floppy image with patched bootloader and sample files"
    )

    find_program(QEMU_I386_EXECUTABLE NAMES qemu-system-i386)
    if(QEMU_I386_EXECUTABLE)
        add_custom_target(run
            COMMAND ${QEMU_I386_EXECUTABLE} -drive file=${FLOPPY_IMAGE},format=raw,if=ide -smp 4 -netdev user,id=net0 -device e1000,netdev=net0 -display none -debugcon stdio
            DEPENDS floppy_image
            COMMENT "Running OS in QEMU..."
        )
    endif()
endif()
