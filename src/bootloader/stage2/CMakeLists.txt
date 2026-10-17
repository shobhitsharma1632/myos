# Stage 2 bootloader CMakeLists.txt

# Sources
file(GLOB SOURCES_C "*.c")
file(GLOB SOURCES_ASM "*.asm")

# Need to set flags that match the old Makefile
set(STAGE2_ASMFLAGS "-f elf")
# Use separate flags to avoid issues with CMake's list parsing
set(STAGE2_CFLAGS_LIST -ffreestanding -nostdlib -m32 -fno-pic -fno-pie)
set(STAGE2_LINKFLAGS_LIST -T "${CMAKE_CURRENT_SOURCE_DIR}/linker.ld" -nostdlib)

# Output
set(STAGE2_BIN "${CMAKE_CURRENT_BINARY_DIR}/stage2.bin")

# Objects
set(OBJECTS "")

# Compile ASM
foreach(asm_file ${SOURCES_ASM})
    get_filename_component(asm_name ${asm_file} NAME_WE)
    set(obj_file "${CMAKE_CURRENT_BINARY_DIR}/${asm_name}.obj")
    add_custom_command(
        OUTPUT ${obj_file}
        COMMAND ${CMAKE_ASM_COMPILER} ${STAGE2_ASMFLAGS} -o ${obj_file} ${asm_file}
        DEPENDS ${asm_file}
    )
    list(APPEND OBJECTS ${obj_file})
endforeach()

# Compile C
foreach(c_file ${SOURCES_C})
    get_filename_component(c_name ${c_file} NAME_WE)
    set(obj_file "${CMAKE_CURRENT_BINARY_DIR}/${c_name}.obj")
    add_custom_command(
        OUTPUT ${obj_file}
        COMMAND ${CMAKE_C_COMPILER} ${STAGE2_CFLAGS_LIST} -I${CMAKE_SOURCE_DIR}/src/libs -I${CMAKE_CURRENT_SOURCE_DIR} -c -o ${obj_file} ${c_file}
        DEPENDS ${c_file}
    )
    list(APPEND OBJECTS ${obj_file})
endforeach()

# Link everything - need to map this carefully.
# The linker script expects to produce a binary, not an ELF.
add_custom_command(
    OUTPUT ${STAGE2_BIN}
    COMMAND ${CMAKE_C_COMPILER} ${STAGE2_LINKFLAGS_LIST} -o ${STAGE2_BIN} ${OBJECTS} -lgcc
    DEPENDS ${OBJECTS} ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    COMMENT "Linking stage2.bin"
)

# Fix: Use full path to the executable to ensure it's found
add_custom_target(bootloader_stage2 ALL DEPENDS ${STAGE2_BIN})
