#include "elf.h"
#include "fat.h"
#include "memdefs.h"
#include "memory.h"
#include "minmax.h"
#include "stdio.h"

bool ELF_Read(Partition* disk, const char* path, void** entryPoint)
{
    uint8_t* headerBuffer = (uint8_t*)MEMORY_ELF_ADDR;
    uint8_t* loadBuffer = (uint8_t*)MEMORY_LOAD_KERNEL;
    uint32_t filePos = 0;
    uint32_t read;

    // Read header
    FAT_File* fd = FAT_Open(disk, path);
    if (!fd)
    {
        printf("ELF file not found: %s\r\n", path);
        return false;
    }

    if ((read = FAT_Read(disk, fd, sizeof(ELFHeader), headerBuffer)) != sizeof(ELFHeader))
    {
        printf("ELF header read error!\r\n");
        FAT_Close(fd);
        return false;
    }
    filePos += read;

    // Validate header
    ELFHeader* header = (ELFHeader*)headerBuffer;
    if (memcmp(header->Magic, ELF_MAGIC, 4) != 0 ||
        header->Bitness != ELF_BITNESS_32BIT ||
        header->Endianness != ELF_ENDIANNESS_LITTLE ||
        header->ELFHeaderVersion != 1 ||
        header->ELFVersion != 1 ||
        header->Type != ELF_TYPE_EXECUTABLE ||
        header->InstructionSet != ELF_INSTRUCTION_SET_X86)
    {
        printf("ELF validation failed!\r\n");
        FAT_Close(fd);
        return false;
    }

    *entryPoint = (void*)header->ProgramEntryPosition;

    // Load program header table
    uint32_t programHeaderOffset = header->ProgramHeaderTablePosition;
    uint32_t programHeaderSize = header->ProgramHeaderTableEntrySize * header->ProgramHeaderTableEntryCount;
    uint32_t programHeaderTableEntrySize = header->ProgramHeaderTableEntrySize;
    uint32_t programHeaderTableEntryCount = header->ProgramHeaderTableEntryCount;

    if (programHeaderOffset > filePos)
    {
        filePos += FAT_Read(disk, fd, programHeaderOffset - filePos, headerBuffer);
    }

    if ((read = FAT_Read(disk, fd, programHeaderSize, headerBuffer)) != programHeaderSize)
    {
        printf("ELF Program Header read error!\r\n");
        FAT_Close(fd);
        return false;
    }
    filePos += read;
    FAT_Close(fd);

    // Parse program header entries
    for (uint32_t i = 0; i < programHeaderTableEntryCount; i++)
    {
        ELFProgramHeader* progHeader = (ELFProgramHeader*)(headerBuffer + i * programHeaderTableEntrySize);
        if (progHeader->Type == ELF_PROGRAM_TYPE_LOAD)
        {
            uint8_t* virtAddress = (uint8_t*)progHeader->VirtualAddress;
            memset(virtAddress, 0, progHeader->MemorySize);
            
            fd = FAT_Open(disk, path);
            if (!fd)
            {
                printf("ELF reopen error!\r\n");
                return false;
            }

            // Seek to segment offset
            uint32_t offset = progHeader->Offset;
            while (offset > 0) 
            {
                uint32_t shouldRead = min(offset, MEMORY_LOAD_SIZE);
                read = FAT_Read(disk, fd, shouldRead, loadBuffer);
                if (read != shouldRead) {
                    printf("ELF seek error!\r\n");
                    FAT_Close(fd);
                    return false;
                }
                offset -= read;
            }

            // Read segment data into memory
            uint32_t fileSize = progHeader->FileSize;
            while (fileSize > 0) 
            {
                uint32_t shouldRead = min(fileSize, MEMORY_LOAD_SIZE);
                read = FAT_Read(disk, fd, shouldRead, loadBuffer);
                if (read != shouldRead) {
                    printf("ELF load segment error!\r\n");
                    FAT_Close(fd);
                    return false;
                }
                fileSize -= read;

                memcpy(virtAddress, loadBuffer, read);
                virtAddress += read;
            }

            FAT_Close(fd);
        }
    }

    return true;
}
