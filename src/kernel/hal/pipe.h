#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PIPE_BUFFER_SIZE 1024

typedef struct Pipe Pipe;

struct Pipe {
    uint8_t buffer[PIPE_BUFFER_SIZE];
    size_t  read_pos;
    size_t  write_pos;
    size_t  count;
    int     readers;
    int     writers;
};

Pipe* Pipe_Create(void);
int   Pipe_Read(Pipe* pipe, void* buf, size_t count);
int   Pipe_Write(Pipe* pipe, const void* buf, size_t count);
void  Pipe_CloseReader(Pipe* pipe);
void  Pipe_CloseWriter(Pipe* pipe);
void  Pipe_RetainReader(Pipe* pipe);
void  Pipe_RetainWriter(Pipe* pipe);
