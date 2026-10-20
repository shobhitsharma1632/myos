#include "pipe.h"
#include "heap.h"
#include "task.h"
#include <memory.h>
#include <debug.h>

#define MODULE "Pipe"

Pipe* Pipe_Create(void)
{
    Pipe* pipe = (Pipe*)kmalloc(sizeof(Pipe));
    if (!pipe)
    {
        log_err(MODULE, "Failed to allocate memory for Pipe");
        return NULL;
    }

    memset(pipe, 0, sizeof(Pipe));
    pipe->readers = 1;
    pipe->writers = 1;
    return pipe;
}

int Pipe_Read(Pipe* pipe, void* buf, size_t count)
{
    if (!pipe || !buf || count == 0)
        return 0;

    uint8_t* dst = (uint8_t*)buf;

    // Wait until data is available or all writers close
    while (pipe->count == 0)
    {
        if (pipe->writers <= 0)
        {
            return 0; // EOF: No writers left and pipe is empty
        }
        Task_Yield();
    }

    size_t bytes_to_read = count;
    if (bytes_to_read > pipe->count)
        bytes_to_read = pipe->count;

    for (size_t i = 0; i < bytes_to_read; i++)
    {
        dst[i] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUFFER_SIZE;
    }

    pipe->count -= bytes_to_read;
    return (int)bytes_to_read;
}

int Pipe_Write(Pipe* pipe, const void* buf, size_t count)
{
    if (!pipe || !buf || count == 0)
        return 0;

    // Check if readers are present (EPIPE)
    if (pipe->readers <= 0)
    {
        return -1;
    }

    const uint8_t* src = (const uint8_t*)buf;
    size_t written = 0;

    while (written < count)
    {
        // Wait until buffer has space
        while (pipe->count >= PIPE_BUFFER_SIZE)
        {
            if (pipe->readers <= 0)
            {
                return written > 0 ? (int)written : -1; // Broken pipe
            }
            Task_Yield();
        }

        pipe->buffer[pipe->write_pos] = src[written++];
        pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUFFER_SIZE;
        pipe->count++;
    }

    return (int)written;
}

void Pipe_RetainReader(Pipe* pipe)
{
    if (pipe) pipe->readers++;
}

void Pipe_RetainWriter(Pipe* pipe)
{
    if (pipe) pipe->writers++;
}

void Pipe_CloseReader(Pipe* pipe)
{
    if (!pipe) return;
    pipe->readers--;
    if (pipe->readers <= 0 && pipe->writers <= 0)
    {
        kfree(pipe);
    }
}

void Pipe_CloseWriter(Pipe* pipe)
{
    if (!pipe) return;
    pipe->writers--;
    if (pipe->readers <= 0 && pipe->writers <= 0)
    {
        kfree(pipe);
    }
}
