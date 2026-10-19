#pragma once
#include <stdint.h>
#include <stddef.h>

extern const uint8_t user_program_hello[];
extern const uint8_t user_program_hello_end[];

extern const uint8_t user_program_counter[];
extern const uint8_t user_program_counter_end[];

extern const uint8_t user_program_fault_gp[];
extern const uint8_t user_program_fault_gp_end[];

extern const uint8_t user_program_fault_pf[];
extern const uint8_t user_program_fault_pf_end[];

#define USER_PROG_SIZE(start, end) ((size_t)((uint32_t)(end) - (uint32_t)(start)))
