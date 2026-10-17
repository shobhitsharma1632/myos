# src/kernel/CMakeLists.txt

# 1. Generate ISRs
find_program(BASH_EXECUTABLE NAMES bash REQUIRED)
set(ISR_GEN_SCRIPT "${CMAKE_SOURCE_DIR}/build_scripts/generate_isrs.sh")
set(ISR_GEN_C "${CMAKE_CURRENT_SOURCE_DIR}/arch/i686/isrs_gen.c")
set(ISR_GEN_INC "${CMAKE_CURRENT_SOURCE_DIR}/arch/i686/isrs_gen.inc")

add_custom_command(
    OUTPUT ${ISR_GEN_C} ${ISR_GEN_INC}
    COMMAND ${BASH_EXECUTABLE} ${ISR_GEN_SCRIPT} ${ISR_GEN_C} ${ISR_GEN_INC}
    DEPENDS ${ISR_GEN_SCRIPT}
    COMMENT "Generating ISRs"
    VERBATIM
)

add_custom_target(generate_isrs DEPENDS ${ISR_GEN_C} ${ISR_GEN_INC})

# 2. Collect sources
file(GLOB_RECURSE SOURCES_C "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
list(FILTER SOURCES_C EXCLUDE REGEX "isrs_gen\\.c$")
file(GLOB_RECURSE SOURCES_ASM "${CMAKE_CURRENT_SOURCE_DIR}/*.asm")
list(FILTER SOURCES_ASM EXCLUDE REGEX "crti\\.asm$")
list(FILTER SOURCES_ASM EXCLUDE REGEX "crtn\\.asm$")

# 3. Compilation flags
set(KERNEL_CFLAGS -ffreestanding -nostdlib -m32 -fno-pic -fno-pie)
set(KERNEL_LINKFLAGS -T "${CMAKE_CURRENT_SOURCE_DIR}/linker.ld" -nostdlib -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/kernel.map)

# Output
set(KERNEL_ELF "${CMAKE_CURRENT_BINARY_DIR}/kernel.elf")

# CRT Objects
set(CRTI_ASM "${CMAKE_CURRENT_SOURCE_DIR}/crti.asm")
set(CRTI_OBJ "${CMAKE_CURRENT_BINARY_DIR}/crti.obj")
add_custom_command(
    OUTPUT ${CRTI_OBJ}
    COMMAND ${CMAKE_ASM_COMPILER} -f elf -I ${CMAKE_CURRENT_SOURCE_DIR} -o ${CRTI_OBJ} ${CRTI_ASM}
    DEPENDS ${CRTI_ASM}
)

set(CRTN_ASM "${CMAKE_CURRENT_SOURCE_DIR}/crtn.asm")
set(CRTN_OBJ "${CMAKE_CURRENT_BINARY_DIR}/crtn.obj")
add_custom_command(
    OUTPUT ${CRTN_OBJ}
    COMMAND ${CMAKE_ASM_COMPILER} -f elf -I ${CMAKE_CURRENT_SOURCE_DIR} -o ${CRTN_OBJ} ${CRTN_ASM}
    DEPENDS ${CRTN_ASM}
)

execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=crtbegin.o
    OUTPUT_VARIABLE CRTBEGIN_OBJ
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=crtend.o
    OUTPUT_VARIABLE CRTEND_OBJ
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Objects list
set(OBJECTS "")

# Compile regular ASM files
foreach(asm_file ${SOURCES_ASM})
    get_filename_component(asm_name ${asm_file} NAME_WE)
    get_filename_component(asm_dir ${asm_file} DIRECTORY)
    file(RELATIVE_PATH asm_rel_dir ${CMAKE_CURRENT_SOURCE_DIR} ${asm_dir})

    set(obj_dir "${CMAKE_CURRENT_BINARY_DIR}/asm/${asm_rel_dir}")
    file(MAKE_DIRECTORY ${obj_dir})
    set(obj_file "${obj_dir}/${asm_name}.obj")

    add_custom_command(
        OUTPUT ${obj_file}
        COMMAND ${CMAKE_ASM_COMPILER} -f elf -I ${CMAKE_CURRENT_SOURCE_DIR} -o ${obj_file} ${asm_file}
        DEPENDS ${asm_file}
    )
    list(APPEND OBJECTS ${obj_file})
endforeach()

# Compile C files
foreach(c_file ${SOURCES_C})
    get_filename_component(c_name ${c_file} NAME_WE)
    get_filename_component(c_dir ${c_file} DIRECTORY)
    file(RELATIVE_PATH c_rel_dir ${CMAKE_CURRENT_SOURCE_DIR} ${c_dir})

    set(obj_dir "${CMAKE_CURRENT_BINARY_DIR}/c/${c_rel_dir}")
    file(MAKE_DIRECTORY ${obj_dir})
    set(obj_file "${obj_dir}/${c_name}.obj")

    add_custom_command(
        OUTPUT ${obj_file}
        COMMAND ${CMAKE_C_COMPILER} ${KERNEL_CFLAGS} -I${CMAKE_CURRENT_SOURCE_DIR} -I${CMAKE_SOURCE_DIR}/src/libs -c -o ${obj_file} ${c_file}
        DEPENDS ${c_file} ${ISR_GEN_C} ${ISR_GEN_INC}
    )
    list(APPEND OBJECTS ${obj_file})
endforeach()

# Include explicit object for generated ISRs
set(ISRS_GEN_C_FILE "${CMAKE_CURRENT_SOURCE_DIR}/arch/i686/isrs_gen.c")
set(ISRS_GEN_OBJ "${CMAKE_CURRENT_BINARY_DIR}/c/arch/i686/isrs_gen.obj")

add_custom_command(
    OUTPUT ${ISRS_GEN_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${KERNEL_CFLAGS} -I${CMAKE_CURRENT_SOURCE_DIR} -I${CMAKE_SOURCE_DIR}/src/libs -c -o ${ISRS_GEN_OBJ} ${ISRS_GEN_C_FILE}
    DEPENDS ${ISRS_GEN_C_FILE} ${ISR_GEN_INC}
)
list(APPEND OBJECTS ${ISRS_GEN_OBJ})

# Order CRT objects: crti.obj -> crtbegin.o -> [kernel objects] -> crtend.o -> crtn.obj
set(ALL_KERNEL_OBJECTS
    ${CRTI_OBJ}
    ${CRTBEGIN_OBJ}
    ${OBJECTS}
    ${CRTEND_OBJ}
    ${CRTN_OBJ}
)

# Link ELF
add_custom_command(
    OUTPUT ${KERNEL_ELF}
    COMMAND ${CMAKE_C_COMPILER} ${KERNEL_LINKFLAGS} -o ${KERNEL_ELF} ${ALL_KERNEL_OBJECTS} -lgcc
    DEPENDS ${ALL_KERNEL_OBJECTS} ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    COMMENT "Linking kernel.elf"
)

add_custom_target(kernel ALL DEPENDS ${KERNEL_ELF})
add_dependencies(kernel generate_isrs)
