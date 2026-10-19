#pragma once

#include <hal/pmm.h>
#include <hal/vmm.h>
#include <hal/vfs.h>
#include <hal/fat.h>
#include <hal/heap.h>
#include <hal/task.h>
#include <hal/shell.h>
#include <hal/gfx.h>
#include <hal/gui.h>
#include <arch/i686/keyboard.h>
#include <arch/i686/pci.h>
#include <arch/i686/bga.h>
#include <arch/i686/mouse.h>
#include <arch/i686/ata.h>

void HAL_Initialize();