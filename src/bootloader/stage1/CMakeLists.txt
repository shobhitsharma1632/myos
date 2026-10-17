# Rule: Assemble and link bootloader stage 1 using NASM and cross-compiler
find_program(NASM_EXECUTABLE NAMES nasm REQUIRED)

set(BOOT_ASM "${CMAKE_CURRENT_SOURCE_DIR}/boot.asm")
set(BOOT_OBJ "${CMAKE_CURRENT_BINARY_DIR}/boot.obj")
set(BOOT_BIN "${CMAKE_CURRENT_BINARY_DIR}/boot.bin")
set(BOOT_MAP "${CMAKE_CURRENT_BINARY_DIR}/boot.map")
set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/linker.ld")

add_custom_command(
    OUTPUT ${BOOT_OBJ}
    COMMAND ${NASM_EXECUTABLE} -f elf ${BOOT_ASM} -o ${BOOT_OBJ}
    DEPENDS ${BOOT_ASM}
    COMMENT "Assembling bootloader stage 1"
    VERBATIM
)

add_custom_command(
    OUTPUT ${BOOT_BIN} ${BOOT_MAP}
    COMMAND ${CMAKE_C_COMPILER} -nostdlib -T ${LINKER_SCRIPT} -Wl,-Map=${BOOT_MAP} -o ${BOOT_BIN} ${BOOT_OBJ}
    DEPENDS ${BOOT_OBJ} ${LINKER_SCRIPT}
    COMMENT "Linking bootloader stage 1"
    VERBATIM
)

add_custom_target(bootloader_stage1 ALL DEPENDS ${BOOT_BIN})
