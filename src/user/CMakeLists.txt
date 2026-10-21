# src/user/CMakeLists.txt

set(USER_CFLAGS -m32 -ffreestanding -nostdlib -fno-pic -fno-pie -I${CMAKE_CURRENT_SOURCE_DIR})
set(USER_LINKFLAGS -m32 -nostdlib -T "${CMAKE_CURRENT_SOURCE_DIR}/linker.ld" -Wl,--build-id=none)

# Assemble crt0.asm
set(CRT0_ASM "${CMAKE_CURRENT_SOURCE_DIR}/crt0.asm")
set(CRT0_OBJ "${CMAKE_CURRENT_BINARY_DIR}/crt0.obj")
add_custom_command(
    OUTPUT ${CRT0_OBJ}
    COMMAND ${CMAKE_ASM_COMPILER} -f elf -o ${CRT0_OBJ} ${CRT0_ASM}
    DEPENDS ${CRT0_ASM}
    COMMENT "Assembling user crt0.asm"
)

# Compile libuser.c
set(LIBUSER_C "${CMAKE_CURRENT_SOURCE_DIR}/libuser.c")
set(LIBUSER_OBJ "${CMAKE_CURRENT_BINARY_DIR}/libuser.obj")
add_custom_command(
    OUTPUT ${LIBUSER_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${USER_CFLAGS} -c -o ${LIBUSER_OBJ} ${LIBUSER_C}
    DEPENDS ${LIBUSER_C} "${CMAKE_CURRENT_SOURCE_DIR}/libuser.h"
    COMMENT "Compiling libuser.c"
)

# Macro to build a user application ELF
macro(build_user_app app_name)
    set(APP_C "${CMAKE_CURRENT_SOURCE_DIR}/apps/${app_name}.c")
    set(APP_OBJ "${CMAKE_CURRENT_BINARY_DIR}/${app_name}.obj")
    set(APP_ELF "${CMAKE_CURRENT_BINARY_DIR}/${app_name}.elf")

    add_custom_command(
        OUTPUT ${APP_OBJ}
        COMMAND ${CMAKE_C_COMPILER} ${USER_CFLAGS} -c -o ${APP_OBJ} ${APP_C}
        DEPENDS ${APP_C} "${CMAKE_CURRENT_SOURCE_DIR}/libuser.h"
        COMMENT "Compiling user app: ${app_name}.c"
    )

    add_custom_command(
        OUTPUT ${APP_ELF}
        COMMAND ${CMAKE_C_COMPILER} ${USER_LINKFLAGS} -o ${APP_ELF} ${CRT0_OBJ} ${APP_OBJ} ${LIBUSER_OBJ} -lgcc
        DEPENDS ${CRT0_OBJ} ${APP_OBJ} ${LIBUSER_OBJ} "${CMAKE_CURRENT_SOURCE_DIR}/linker.ld"
        COMMENT "Linking user app: ${app_name}.elf"
    )

    set(USER_APP_${app_name}_ELF ${APP_ELF} PARENT_SCOPE)
    list(APPEND ALL_USER_ELFS ${APP_ELF})
endmacro()

set(ALL_USER_ELFS "")
build_user_app(hello)
build_user_app(calc)
build_user_app(counter)
build_user_app(cat)
build_user_app(wc)
build_user_app(grep)
build_user_app(sh)
build_user_app(snake)
build_user_app(ifconfig)
build_user_app(ping)
build_user_app(tee)
build_user_app(head)
build_user_app(curl)
build_user_app(httpd)

set(ALL_USER_ELFS ${ALL_USER_ELFS} PARENT_SCOPE)
add_custom_target(user_apps ALL DEPENDS ${ALL_USER_ELFS})
