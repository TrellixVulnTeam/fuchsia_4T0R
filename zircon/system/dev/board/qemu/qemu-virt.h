// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <ddk/protocol/platform/bus.h>

// BTI IDs for our devices
enum {
  BTI_SYSMEM,
};

/* up to 30 GB of ram */
#define MEMORY_BASE_PHYS (0x40000000)

/* memory map of peripherals, from qemu hw/arm/virt.c */
#if 0
static const MemMapEntry a15memmap[] = {
    /* Space up to 0x8000000 is reserved for a boot ROM */
    [VIRT_FLASH] =              {          0, 0x08000000 },
    [VIRT_CPUPERIPHS] =         { 0x08000000, 0x00020000 },
    /* GIC distributor and CPU interfaces sit inside the CPU peripheral space */
    [VIRT_GIC_DIST] =           { 0x08000000, 0x00010000 },
    [VIRT_GIC_CPU] =            { 0x08010000, 0x00010000 },
    [VIRT_GIC_V2M] =            { 0x08020000, 0x00001000 },
    [VIRT_UART] =               { 0x09000000, 0x00001000 },
    [VIRT_RTC] =                { 0x09010000, 0x00001000 },
    [VIRT_FW_CFG] =             { 0x09020000, 0x0000000a },
    [VIRT_MMIO] =               { 0x0a000000, 0x00000200 },
    /* ...repeating for a total of NUM_VIRTIO_TRANSPORTS, each of that size */
    [VIRT_PLATFORM_BUS] =       { 0x0c000000, 0x02000000 },
    [VIRT_PCIE_MMIO] =          { 0x10000000, 0x2eff0000 },
    [VIRT_PCIE_PIO] =           { 0x3eff0000, 0x00010000 },
    [VIRT_PCIE_ECAM] =          { 0x3f000000, 0x01000000 },
    [VIRT_MEM] =                { 0x40000000, 30ULL * 1024 * 1024 * 1024 },
};

static const int a15irqmap[] = {
    [VIRT_UART] = 1,
    [VIRT_RTC] = 2,
    [VIRT_PCIE] = 3, /* ... to 6 */
    [VIRT_MMIO] = 16, /* ...to 16 + NUM_VIRTIO_TRANSPORTS - 1 */
    [VIRT_GIC_V2M] = 48, /* ...to 48 + NUM_GICV2M_SPIS - 1 */
    [VIRT_PLATFORM_BUS] = 112, /* ...to 112 + PLATFORM_BUS_NUM_IRQS -1 */
};
#endif

/* map all of 0-1GB into kernel space in one shot */
#define PERIPHERAL_BASE_PHYS (0)
#define PERIPHERAL_BASE_SIZE (0x40000000UL)  // 1GB

#define PERIPHERAL_BASE_VIRT (0xffffffffc0000000ULL)  // -1GB

/* individual peripherals in this mapping */
#define CPUPRIV_BASE_VIRT (PERIPHERAL_BASE_VIRT + 0x08000000)
#define CPUPRIV_BASE_PHYS (PERIPHERAL_BASE_PHYS + 0x08000000)
#define CPUPRIV_SIZE (0x00020000)
#define UART_BASE (PERIPHERAL_BASE_VIRT + 0x09000000)
#define UART_SIZE (0x00001000)
#define RTC_BASE (PERIPHERAL_BASE_VIRT + 0x09010000)
#define RTC_BASE_PHYS (PERIPHERAL_BASE_PHYS + 0x09010000)
#define RTC_SIZE (0x00001000)
#define FW_CFG_BASE (PERIPHERAL_BASE_VIRT + 0x09020000)
#define FW_CFG_SIZE (0x00001000)
#define NUM_VIRTIO_TRANSPORTS 32
#define VIRTIO_BASE (PERIPHERAL_BASE_VIRT + 0x0a000000)
#define VIRTIO_SIZE (NUM_VIRTIO_TRANSPORTS * 0x200)
#define PCIE_MMIO_BASE_PHYS ((zx_paddr_t)(PERIPHERAL_BASE_PHYS + 0x10000000))
#define PCIE_MMIO_SIZE (0x2eff0000)
#define PCIE_PIO_BASE_PHYS ((zx_paddr_t)(PERIPHERAL_BASE_PHYS + 0x3eff0000))
#define PCIE_PIO_SIZE (0x00010000)
#define PCIE_ECAM_BASE_PHYS ((zx_paddr_t)(PERIPHERAL_BASE_PHYS + 0x3f000000))
#define PCIE_ECAM_SIZE (0x01000000)
#define GICV2M_FRAME_PHYS (PERIPHERAL_BASE_PHYS + 0x08020000)

// Unused MMIO ranges for test drivers
#define TEST_MMIO_1 (PERIPHERAL_BASE_PHYS + 0)
#define TEST_MMIO_1_SIZE 0x1000
#define TEST_MMIO_2 (PERIPHERAL_BASE_PHYS + 0x1000)
#define TEST_MMIO_2_SIZE 0x2000
#define TEST_MMIO_3 (PERIPHERAL_BASE_PHYS + 0x3000)
#define TEST_MMIO_3_SIZE 0x3000
#define TEST_MMIO_4 (PERIPHERAL_BASE_PHYS + 0x6000)
#define TEST_MMIO_4_SIZE 0x4000

/* interrupts */
#define ARM_GENERIC_TIMER_VIRTUAL_INT 27
#define ARM_GENERIC_TIMER_PHYSICAL_INT 30
#define UART0_INT (32 + 1)
#define RTC_INT (32 + 2)
#define PCIE_INT_BASE (32 + 3)
#define PCIE_INT_COUNT (4)
#define VIRTIO0_INT (32 + 16)

#define MAX_INT 288

typedef struct {
  pbus_protocol_t pbus;
} qemu_bus_t;

// qemu-sysmem.c
zx_status_t qemu_sysmem_init(qemu_bus_t* bus);
