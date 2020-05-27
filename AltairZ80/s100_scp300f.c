/*************************************************************************
 *                                                                       *
 * $Id: s100_scp300f.c 1940 2008-06-13 05:28:57Z hharte $                *
 *                                                                       *
 * Copyright (c) 2007-2008 Howard M. Harte.                              *
 * http://www.hartetec.com                                               *
 *                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining *
 * a copy of this software and associated documentation files (the       *
 * "Software"), to deal in the Software without restriction, including   *
 * without limitation the rights to use, copy, modify, merge, publish,   *
 * distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to *
 * the following conditions:                                             *
 *                                                                       *
 * The above copyright notice and this permission notice shall be        *
 * included in all copies or substantial portions of the Software.       *
 *                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                 *
 * NONINFRINGEMENT. IN NO EVENT SHALL HOWARD M. HARTE BE LIABLE FOR ANY  *
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  *
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     *
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                *
 *                                                                       *
 * Except as contained in this notice, the name of Howard M. Harte shall *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * Howard M. Harte.                                                      *
 *                                                                       *
 * SIMH Interface based on altairz80_hdsk.c, by Peter Schorn.            *
 *                                                                       *
 * Module Description:                                                   *
 *     Seattle Computer Products SCP300F Support Board module for SIMH.  *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

/* #define DBG_MSG */

#include "altairz80_defs.h"

#ifdef DBG_MSG
#define DBG_PRINT(args) sim_printf args
#else
#define DBG_PRINT(args)
#endif

/* Debug flags */
#define UART_MSG    (1 << 0)
#define ROM_MSG     (1 << 1)
#define VERBOSE_MSG (1 << 2)

#define SCP300F_MAX_DRIVES  1
#define SCP300F_ROM_SIZE    (2048)
#define SCP300F_ADDR_MASK   (SCP300F_ROM_SIZE - 1)

#define SCP300F_IO_SIZE     (16)
#define SCP300F_IO_MASK     (SCP300F_IO_SIZE - 1)

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
    uint8 *ram;
    uint8 *rom;
    uint8 rom_enabled;
} SCP300F_INFO;

static SCP300F_INFO scp300f_info_data = { { 0xFF800, SCP300F_ROM_SIZE, 0xF0, SCP300F_IO_SIZE } };
static SCP300F_INFO *scp300f_info = &scp300f_info_data;

extern t_stat set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);
extern uint32 PCX;
extern int32 find_unit_index (UNIT *uptr);

static t_stat scp300f_reset(DEVICE *scp300f_dev);

static uint8 SCP300F_Read(const uint32 Addr);
static uint8 SCP300F_Write(const uint32 Addr, uint8 cData);
static const char* scp300f_description(DEVICE *dptr);

static int32 scp300fdev(const int32 port, const int32 io, const int32 data);
static int32 scp300f_mem(const int32 port, const int32 io, const int32 data);

static int32 scp300f_sr = 0x00;     /* Sense Switch Register, 0=Monitor prompt, 1=disk boot */

static UNIT scp300f_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE, 0) }
};

static REG scp300f_reg[] = {
    { HRDATAD (SR, scp300f_sr, 8, "Sense switch register, 0=monitor prompt, 1=disk boot"), },
    { NULL }
};

static const char* scp300f_description(DEVICE *dptr) {
    return "SCP Support Board";
}

static MTAB scp300f_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "MEMBASE",  "MEMBASE",
        &set_membase, &show_membase, NULL, "Sets support module memory base address"    },
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets support module I/O base address"         },
    { 0 }
};

/* Debug Flags */
static DEBTAB scp300f_dt[] = {
    { "UART",       UART_MSG,       "UART messages"     },
    { "ROM",        ROM_MSG,        "ROM messages"      },
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { NULL,         0                                   }
};


DEVICE scp300f_dev = {
    "SCP300F", scp300f_unit, scp300f_reg, scp300f_mod,
    SCP300F_MAX_DRIVES, 10, 31, 1, SCP300F_MAX_DRIVES, SCP300F_MAX_DRIVES,
    NULL, NULL, &scp300f_reset,
    NULL, NULL, NULL,
    &scp300f_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), 0,
    scp300f_dt, NULL, NULL, NULL, NULL, NULL, &scp300f_description
};

/* Reset routine */
static t_stat scp300f_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    sim_debug(VERBOSE_MSG, &scp300f_dev, "SCP300F: Reset.\n");

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &scp300fdev, "scp300fdev", TRUE);
        sim_map_resource(pnp->mem_base, pnp->mem_size, RESOURCE_TYPE_MEMORY, &scp300f_mem, "scp300f_mem", TRUE);
    } else {
        /* Connect SCP300F at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &scp300fdev, "scp300fdev", FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
        /* Connect SCP300F Memory (512K RAM, 1MB FLASH) */
        if(sim_map_resource(pnp->mem_base, pnp->mem_size, RESOURCE_TYPE_MEMORY, &scp300f_mem, "scp300f_mem", FALSE) != 0) {
            sim_printf("%s: error mapping MEM resource at 0x%04x\n", __FUNCTION__, pnp->mem_base);
            return SCPE_ARG;
        }

        /* Re-enable ROM */
        scp300f_info->rom_enabled = 1;
    }
    return SCPE_OK;
}


static uint8 scp300f_ram[SCP300F_ROM_SIZE];

/* ; Seattle Computer Products 8086 Monitor version 1.5  3-19-82.
 * ;   by Tim Paterson
 * ; This software is not copyrighted.
 *
 * This was assembled from source (MON.ASM) using 86DOS ASM.COM running under Windows XP.
 * It is configured for a Cromemco 16FDC disk controller.
 */
static uint8 scp300f_rom[SCP300F_ROM_SIZE] = {
    0xFC, 0x33, 0xC0, 0x8E, 0xD0, 0x8E, 0xD8, 0x8E, 0xC0, 0xBF, 0x9C, 0x01, 0xB9, 0x0E, 0x00, 0xF3,
    0xAB, 0x80, 0x0E, 0xB7, 0x01, 0x02, 0xB1, 0x04, 0xB0, 0x40, 0xBF, 0xAC, 0x01, 0xF3, 0xAB, 0xC6,
    0x06, 0xA5, 0x01, 0x0C, 0xBC, 0x9C, 0x01, 0xB0, 0x17, 0xE6, 0xF5, 0xB0, 0xF3, 0xE6, 0xF4, 0xB8,
    0x84, 0x05, 0xE7, 0xF4, 0xBE, 0x33, 0x07, 0xBA, 0xF0, 0x00, 0x2E, 0xAC, 0x8A, 0xC8, 0xE3, 0x05,
    0x2E, 0xAC, 0xEE, 0xE2, 0xFB, 0x42, 0x80, 0xFA, 0xF8, 0x75, 0xEF, 0xE8, 0x19, 0x00, 0xBE, 0xF5,
    0x07, 0xB8, 0x23, 0xE8, 0xE7, 0xF4, 0xB0, 0x0D, 0xE6, 0xF5, 0x2E, 0xAD, 0xE6, 0xF4, 0x8A, 0xC4,
    0xE6, 0xF4, 0xE8, 0x02, 0x00, 0xEB, 0xF3, 0xE8, 0x98, 0x00, 0xE8, 0x95, 0x00, 0x3C, 0x0D, 0x74,
    0x01, 0xC3, 0xBF, 0x18, 0x01, 0xC6, 0x05, 0x0D, 0xE4, 0xFF, 0xA8, 0x01, 0x74, 0x03, 0xE9, 0xF5,
    0x06, 0xBE, 0x51, 0x07, 0xE8, 0x8B, 0x00, 0xFC, 0x33, 0xC0, 0x8E, 0xD8, 0x8E, 0xC0, 0xBC, 0x9C,
    0x01, 0xC7, 0x06, 0x64, 0x00, 0xBB, 0x06, 0x8C, 0x0E, 0x66, 0x00, 0xB0, 0x3E, 0xE8, 0xC8, 0x00,
    0xE8, 0x1E, 0x00, 0xE8, 0x7F, 0x00, 0x74, 0xDF, 0x8A, 0x05, 0x2C, 0x42, 0x72, 0x10, 0x3C, 0x13,
    0x73, 0x0C, 0x47, 0xD0, 0xE0, 0x98, 0x93, 0x2E, 0xFF, 0x97, 0x7D, 0x01, 0xEB, 0xC9, 0xE9, 0xA8,
    0x02, 0xBF, 0x18, 0x01, 0x33, 0xC9, 0xE8, 0x39, 0x00, 0x3C, 0x20, 0x72, 0x1B, 0x3C, 0x7F, 0x74,
    0x0E, 0xE8, 0x94, 0x00, 0x3C, 0x40, 0x74, 0x25, 0xAA, 0x41, 0x83, 0xF9, 0x50, 0x76, 0xE7, 0xE3,
    0xE5, 0x4F, 0x49, 0xE8, 0x29, 0x00, 0xEB, 0xDE, 0x3C, 0x08, 0x74, 0xF3, 0x3C, 0x0D, 0x75, 0xD6,
    0xAA, 0xBF, 0x18, 0x01, 0xB0, 0x0D, 0xE8, 0x6F, 0x00, 0xB0, 0x0A, 0xEB, 0x6B, 0xE8, 0xF4, 0xFF,
    0xEB, 0x85, 0xFA, 0xE4, 0xF7, 0xA8, 0x02, 0x74, 0xF9, 0xE4, 0xF6, 0x24, 0x7F, 0xFB, 0xC3, 0xBE,
    0x73, 0x07, 0x2E, 0xAC, 0xE8, 0x51, 0x00, 0xD0, 0xE0, 0x73, 0xF7, 0xC3, 0xE8, 0x06, 0x00, 0x82,
    0x3D, 0x2C, 0x75, 0x0A, 0x47, 0xB0, 0x20, 0x51, 0xB1, 0xFF, 0xF3, 0xAE, 0x4F, 0x59, 0x82, 0x3D,
    0x0D, 0xC3, 0x8C, 0xDA, 0xB4, 0x00, 0xE8, 0x78, 0x00, 0x03, 0xD6, 0xEB, 0x09, 0x8C, 0xC2, 0xB4,
    0x00, 0xE8, 0x6D, 0x00, 0x03, 0xD7, 0x82, 0xD4, 0x00, 0xE8, 0x12, 0x00, 0x8A, 0xC6, 0xE8, 0x02,
    0x00, 0x8A, 0xC2, 0x8A, 0xE0, 0x51, 0xB1, 0x04, 0xD2, 0xE8, 0x59, 0xE8, 0x02, 0x00, 0x8A, 0xC4,
    0x24, 0x0F, 0x04, 0x90, 0x27, 0x14, 0x40, 0x27, 0x50, 0xE4, 0xF7, 0x24, 0x01, 0x74, 0xFA, 0x58,
    0xE6, 0xF6, 0xC3, 0xB0, 0x20, 0xEB, 0xF1, 0xE8, 0xF9, 0xFF, 0xE2, 0xFB, 0xC3, 0x76, 0x07, 0x68,
    0x03, 0x0D, 0x02, 0x88, 0x03, 0x97, 0x02, 0x6A, 0x06, 0x68, 0x03, 0x4C, 0x06, 0x68, 0x03, 0x68,
    0x03, 0x68, 0x03, 0x6A, 0x02, 0x68, 0x03, 0x59, 0x06, 0x68, 0x03, 0x68, 0x03, 0x2F, 0x04, 0xBA,
    0x02, 0x6A, 0x05, 0x8A, 0xC2, 0x24, 0x0F, 0xE8, 0x07, 0x00, 0x8A, 0xD0, 0x8A, 0xC6, 0x32, 0xF6,
    0xC3, 0xD1, 0xE2, 0xD0, 0xD4, 0xD1, 0xE2, 0xD0, 0xD4, 0xD1, 0xE2, 0xD0, 0xD4, 0xD1, 0xE2, 0xD0,
    0xD4, 0xC3, 0xB9, 0x05, 0x00, 0xE8, 0x22, 0x01, 0x50, 0x52, 0xE8, 0x4F, 0xFF, 0x82, 0x3D, 0x4C,
    0x74, 0x1C, 0xBA, 0x80, 0x00, 0xE8, 0x30, 0x01, 0x72, 0x1B, 0xB9, 0x05, 0x00, 0xE8, 0x0A, 0x01,
    0x8B, 0xCA, 0x5A, 0x5B, 0x2B, 0xCA, 0x1A, 0xE7, 0x75, 0x1D, 0x93, 0x41, 0xEB, 0x0B, 0x47, 0xB9,
    0x04, 0x00, 0xE8, 0xF5, 0x00, 0x8B, 0xCA, 0x5A, 0x58, 0x8B, 0xDA, 0x81, 0xE3, 0x0F, 0x00, 0xE3,
    0x04, 0x03, 0xD9, 0x73, 0x9E, 0x74, 0x9C, 0xB8, 0x52, 0x47, 0xE9, 0x1F, 0x03, 0xE8, 0xB2, 0xFF,
    0x50, 0xE8, 0x4E, 0x01, 0x1F, 0x8B, 0xF2, 0xE8, 0x18, 0xFF, 0x56, 0xE8, 0x55, 0xFF, 0xAC, 0xE8,
    0x31, 0xFF, 0x5A, 0x49, 0x74, 0x17, 0x8B, 0xC6, 0xA8, 0x0F, 0x74, 0x0C, 0x52, 0xA8, 0x07, 0x75,
    0xEA, 0xB0, 0x2D, 0xE8, 0x32, 0xFF, 0xEB, 0xE6, 0xE8, 0x02, 0x00, 0xEB, 0xDA, 0x51, 0x8B, 0xC6,
    0x8B, 0xF2, 0x2B, 0xC2, 0x8B, 0xD8, 0xD1, 0xE0, 0x03, 0xC3, 0xB9, 0x33, 0x00, 0x2B, 0xC8, 0xE8,
    0x25, 0xFF, 0x8B, 0xCB, 0xAC, 0x24, 0x7F, 0x3C, 0x7F, 0x74, 0x04, 0x3C, 0x20, 0x73, 0x02, 0xB0,
    0x2E, 0xE8, 0x04, 0xFF, 0xE2, 0xEE, 0x59, 0xE9, 0x8A, 0xFE, 0xE8, 0x55, 0xFF, 0x51, 0x50, 0x8B,
    0xF2, 0xB9, 0x05, 0x00, 0xE8, 0x73, 0x00, 0xE8, 0xE8, 0x00, 0xE8, 0x26, 0xFF, 0x8B, 0xFA, 0x5B,
    0x8E, 0xDB, 0x8E, 0xC0, 0x59, 0x3B, 0xFE, 0x1B, 0xC3, 0x72, 0x07, 0x49, 0x03, 0xF1, 0x03, 0xF9,
    0xFD, 0x41, 0xA4, 0x49, 0xF3, 0xA4, 0xC3, 0xE8, 0x28, 0xFF, 0x51, 0x50, 0x52, 0xE8, 0xB4, 0x00,
    0x5F, 0x07, 0x59, 0x3B, 0xD9, 0xBE, 0x18, 0x01, 0xE3, 0x02, 0x73, 0xE6, 0x2B, 0xCB, 0x87, 0xD9,
    0x57, 0xF3, 0xA4, 0x5E, 0x8B, 0xCB, 0x06, 0x1F, 0xEB, 0xD8, 0xE8, 0x05, 0xFF, 0x51, 0x50, 0x52,
    0xE8, 0x91, 0x00, 0x4B, 0x5F, 0x07, 0x59, 0x2B, 0xCB, 0xBE, 0x18, 0x01, 0xAC, 0xAE, 0xE0, 0xFD,
    0x75, 0xC4, 0x53, 0x87, 0xCB, 0x57, 0xF3, 0xA6, 0x8B, 0xCB, 0x5F, 0x5B, 0x75, 0x08, 0x4F, 0xE8,
    0x5B, 0xFE, 0x47, 0xE8, 0x0E, 0xFE, 0xE3, 0xAE, 0xEB, 0xDF, 0xE8, 0x2F, 0xFE, 0x33, 0xD2, 0x8A,
    0xE6, 0xE8, 0x14, 0x00, 0x72, 0x73, 0x8A, 0xD0, 0x47, 0x49, 0xE8, 0x0B, 0x00, 0x72, 0x97, 0xE3,
    0x68, 0xE8, 0xAD, 0xFE, 0x0A, 0xD0, 0xEB, 0xF0, 0x8A, 0x05, 0x2C, 0x30, 0x72, 0x88, 0x3C, 0x0A,
    0xF5, 0x73, 0x83, 0x2C, 0x07, 0x3C, 0x0A, 0x72, 0x03, 0x3C, 0x10, 0xF5, 0xC3, 0xE8, 0xFC, 0xFD,
    0xE8, 0xE5, 0xFF, 0x72, 0x0B, 0xB9, 0x02, 0x00, 0xE8, 0xBF, 0xFF, 0x88, 0x17, 0x43, 0xF8, 0xC3,
    0x8A, 0x05, 0x3C, 0x27, 0x74, 0x06, 0x3C, 0x22, 0x74, 0x02, 0xF9, 0xC3, 0x8A, 0xE0, 0x47, 0x8A,
    0x05, 0x47, 0x3C, 0x0D, 0x74, 0x23, 0x3A, 0xC4, 0x75, 0x05, 0x3A, 0x25, 0x75, 0xE0, 0x47, 0x88,
    0x07, 0x43, 0xEB, 0xEB, 0xBB, 0x18, 0x01, 0xE8, 0xC3, 0xFF, 0x73, 0xFB, 0x81, 0xEB, 0x18, 0x01,
    0x74, 0x07, 0xE8, 0xC0, 0xFD, 0x75, 0x02, 0xC3, 0x4F, 0x81, 0xEF, 0x17, 0x01, 0x8B, 0xCF, 0xE8,
    0x05, 0xFE, 0xBE, 0x6A, 0x07, 0xE8, 0x9A, 0xFD, 0xE9, 0x0C, 0xFD, 0xE8, 0xD6, 0xFF, 0x5F, 0x07,
    0xBE, 0x18, 0x01, 0x8B, 0xCB, 0xF3, 0xA4, 0xC3, 0xB9, 0x05, 0x00, 0xE8, 0x5C, 0xFF, 0xE8, 0x12,
    0xFE, 0x82, 0xEC, 0x08, 0x80, 0xC6, 0x80, 0x50, 0x52, 0xE8, 0x89, 0xFD, 0x75, 0xDD, 0x5F, 0x07,
    0xE8, 0x9A, 0xFD, 0xE8, 0xCD, 0xFD, 0x26, 0x8A, 0x05, 0xE8, 0xA7, 0xFD, 0xB0, 0x2E, 0xE8, 0xB7,
    0xFD, 0xB9, 0x02, 0x00, 0xBA, 0x00, 0x00, 0xE8, 0x48, 0xFD, 0x8A, 0xE0, 0xE8, 0x4B, 0xFF, 0x86,
    0xE0, 0x72, 0x0C, 0xE8, 0xA2, 0xFD, 0x8A, 0xF2, 0x8A, 0xD4, 0xE2, 0xEB, 0xE8, 0x33, 0xFD, 0x3C,
    0x08, 0x74, 0x19, 0x3C, 0x7F, 0x74, 0x15, 0x3C, 0x2D, 0x74, 0x4D, 0x3C, 0x0D, 0x74, 0x2F, 0x3C,
    0x20, 0x74, 0x31, 0xB0, 0x07, 0xE8, 0x80, 0xFD, 0xE3, 0xE2, 0xEB, 0xCB, 0x82, 0xF9, 0x02, 0x74,
    0xC6, 0xFE, 0xC1, 0x8A, 0xD6, 0x8A, 0xF5, 0xE8, 0x15, 0xFD, 0xEB, 0xBB, 0x82, 0xF9, 0x02, 0x74,
    0x0B, 0x51, 0xB1, 0x04, 0xD2, 0xE6, 0x59, 0x0A, 0xD6, 0x26, 0x88, 0x15, 0x47, 0xC3, 0xE8, 0xEB,
    0xFF, 0xE9, 0xE0, 0xFC, 0xE8, 0xE5, 0xFF, 0x41, 0x41, 0xE8, 0x5B, 0xFD, 0x8B, 0xC7, 0x24, 0x07,
    0x75, 0x84, 0xE8, 0xCF, 0xFC, 0xE9, 0x78, 0xFF, 0xE8, 0xD1, 0xFF, 0x4F, 0x4F, 0xEB, 0xF3, 0xE8,
    0xEA, 0xFC, 0x74, 0x62, 0x8A, 0x15, 0x47, 0x8A, 0x35, 0x82, 0xFE, 0x0D, 0x74, 0x76, 0x47, 0xE8,
    0x20, 0xFF, 0x82, 0xFE, 0x20, 0x74, 0x6D, 0xBF, 0xD7, 0x06, 0x92, 0x0E, 0x07, 0xB9, 0x0E, 0x00,
    0xF2, 0xAF, 0x75, 0x3C, 0x0B, 0xC9, 0x75, 0x06, 0x4F, 0x4F, 0x2E, 0x8B, 0x45, 0xFE, 0xE8, 0x07,
    0xFD, 0x8A, 0xC4, 0xE8, 0x02, 0xFD, 0xE8, 0x0A, 0xFD, 0x1E, 0x07, 0x8D, 0x9D, 0xC3, 0xFA, 0x8B,
    0x17, 0xE8, 0xD8, 0xFC, 0xE8, 0x7D, 0xFC, 0xB0, 0x3A, 0xE8, 0xEC, 0xFC, 0xE8, 0x42, 0xFC, 0xE8,
    0xA3, 0xFC, 0x74, 0x0B, 0xB9, 0x04, 0x00, 0xE8, 0x63, 0xFE, 0xE8, 0xD5, 0xFE, 0x89, 0x17, 0xC3,
    0xB8, 0x42, 0x52, 0xE9, 0x96, 0x00, 0xBE, 0xD7, 0x06, 0xBB, 0x9C, 0x01, 0xB9, 0x08, 0x00, 0xE8,
    0x65, 0x00, 0xE8, 0x4F, 0xFC, 0xB9, 0x05, 0x00, 0xE8, 0x5C, 0x00, 0xE8, 0xC5, 0xFC, 0xE8, 0x93,
    0x00, 0xE9, 0x40, 0xFC, 0x82, 0xFA, 0x46, 0x75, 0xD7, 0xE8, 0x88, 0x00, 0xB0, 0x2D, 0xE8, 0xA7,
    0xFC, 0xE8, 0xFD, 0xFB, 0xE8, 0x5E, 0xFC, 0x33, 0xDB, 0x8B, 0x16, 0xB6, 0x01, 0x8B, 0xF7, 0xAD,
    0x3C, 0x0D, 0x74, 0x66, 0x82, 0xFC, 0x0D, 0x74, 0x66, 0xBF, 0xF3, 0x06, 0xB9, 0x20, 0x00, 0x0E,
    0x07, 0xF2, 0xAF, 0x75, 0x5A, 0x8A, 0xE9, 0x80, 0xE1, 0x0F, 0xB8, 0x01, 0x00, 0xD3, 0xC0, 0x85,
    0xC3, 0x75, 0x33, 0x0B, 0xD8, 0x0B, 0xD0, 0xF6, 0xC5, 0x10, 0x75, 0x02, 0x33, 0xD0, 0x8B, 0xFE,
    0x1E, 0x07, 0xE8, 0x17, 0xFC, 0xEB, 0xC6, 0x2E, 0xAD, 0xE8, 0x5C, 0xFC, 0x8A, 0xC4, 0xE8, 0x57,
    0xFC, 0xB0, 0x3D, 0xE8, 0x52, 0xFC, 0x8B, 0x17, 0x43, 0x43, 0xE8, 0x2F, 0xFC, 0xE8, 0x53, 0xFC,
    0xE8, 0x50, 0xFC, 0xE2, 0xE2, 0xC3, 0xB8, 0x44, 0x46, 0xE8, 0x0E, 0x00, 0xE8, 0x39, 0xFC, 0x8A,
    0xC4, 0xE8, 0x34, 0xFC, 0xBE, 0x6B, 0x07, 0xE9, 0x3B, 0xFE, 0x89, 0x16, 0xB6, 0x01, 0xC3, 0xB8,
    0x42, 0x46, 0xEB, 0xE5, 0xBE, 0xF3, 0x06, 0xB9, 0x10, 0x00, 0x8B, 0x16, 0xB6, 0x01, 0x2E, 0xAD,
    0xD1, 0xE2, 0x72, 0x04, 0x2E, 0x8B, 0x44, 0x1E, 0x0B, 0xC0, 0x74, 0x0B, 0xE8, 0x09, 0xFC, 0x8A,
    0xC4, 0xE8, 0x04, 0xFC, 0xE8, 0x0C, 0xFC, 0xE2, 0xE5, 0xC3, 0xE8, 0xAF, 0xFB, 0xE8, 0x98, 0xFD,
    0xBA, 0x01, 0x00, 0x72, 0x06, 0xB9, 0x04, 0x00, 0xE8, 0x6F, 0xFD, 0x89, 0x16, 0x02, 0x01, 0xE8,
    0xE0, 0xFD, 0xC7, 0x06, 0x00, 0x01, 0x00, 0x00, 0x80, 0x0E, 0xB7, 0x01, 0x01, 0xC7, 0x06, 0x0C,
    0x00, 0xD1, 0x05, 0x8C, 0x0E, 0x0E, 0x00, 0xC7, 0x06, 0x04, 0x00, 0xD8, 0x05, 0x8C, 0x0E, 0x06,
    0x00, 0xFA, 0xC7, 0x06, 0x64, 0x00, 0xD8, 0x05, 0x8C, 0x0E, 0x66, 0x00, 0xBC, 0x9C, 0x01, 0x58,
    0x5B, 0x59, 0x5A, 0x5D, 0x5D, 0x5E, 0x5F, 0x07, 0x07, 0x17, 0x8B, 0x26, 0xA4, 0x01, 0xFF, 0x36,
    0xB6, 0x01, 0xFF, 0x36, 0xB2, 0x01, 0xFF, 0x36, 0xB4, 0x01, 0x8E, 0x1E, 0xAC, 0x01, 0xCF, 0xEB,
    0xB1, 0x87, 0xEC, 0xFF, 0x4E, 0x00, 0x87, 0xEC, 0x2E, 0x89, 0x26, 0xA4, 0x09, 0x2E, 0x8C, 0x16,
    0xB0, 0x09, 0x33, 0xE4, 0x8E, 0xD4, 0xBC, 0xB0, 0x01, 0x06, 0x1E, 0x57, 0x56, 0x55, 0x4C, 0x4C,
    0x52, 0x51, 0x53, 0x50, 0x16, 0x1F, 0x8B, 0x26, 0xA4, 0x01, 0x8E, 0x16, 0xB0, 0x01, 0x8F, 0x06,
    0xB4, 0x01, 0x8F, 0x06, 0xB2, 0x01, 0x58, 0x80, 0xE4, 0xFE, 0xA3, 0xB6, 0x01, 0x89, 0x26, 0xA4,
    0x01, 0x1E, 0x07, 0x1E, 0x17, 0xBC, 0x9C, 0x01, 0xC7, 0x06, 0x64, 0x00, 0xBB, 0x06, 0xB0, 0x20,
    0xE6, 0xF2, 0xFB, 0xFC, 0xE8, 0xCD, 0xFA, 0xE8, 0x6C, 0xFE, 0xFF, 0x0E, 0x02, 0x01, 0x75, 0x9F,
    0xBE, 0x04, 0x01, 0x8B, 0x0E, 0x00, 0x01, 0xE3, 0x10, 0x8B, 0x54, 0x14, 0xAD, 0x50, 0xE8, 0x62,
    0xFB, 0x8E, 0xC0, 0x8B, 0xFA, 0x58, 0xAA, 0xE2, 0xF0, 0xE9, 0x3B, 0xFA, 0xB9, 0x04, 0x00, 0xE8,
    0x98, 0xFC, 0xEC, 0xE8, 0xFD, 0xFA, 0xE9, 0x9B, 0xFA, 0xB9, 0x04, 0x00, 0xE8, 0x8B, 0xFC, 0x52,
    0xB9, 0x02, 0x00, 0xE8, 0x84, 0xFC, 0x92, 0x5A, 0xEE, 0xC3, 0xBB, 0x18, 0x01, 0x33, 0xF6, 0xE8,
    0xAA, 0xFA, 0x74, 0x19, 0xB9, 0x05, 0x00, 0xE8, 0x70, 0xFC, 0x89, 0x17, 0x88, 0x67, 0xED, 0x43,
    0x43, 0x46, 0x83, 0xFE, 0x0B, 0x75, 0xE8, 0xB8, 0x42, 0x50, 0xE9, 0x9F, 0xFE, 0x89, 0x36, 0x00,
    0x01, 0xE8, 0xCE, 0xFC, 0x8B, 0xCE, 0xE3, 0x1A, 0xBE, 0x04, 0x01, 0x8B, 0x54, 0x14, 0xAD, 0xE8,
    0x01, 0xFB, 0x8E, 0xD8, 0x8B, 0xFA, 0x8A, 0x05, 0xC6, 0x05, 0xCC, 0x06, 0x1F, 0x88, 0x44, 0xFE,
    0xE2, 0xE9, 0xC7, 0x06, 0x02, 0x01, 0x01, 0x00, 0xE9, 0xD2, 0xFE, 0x50, 0xB0, 0x20, 0xE6, 0xF2,
    0xE4, 0xF6, 0x24, 0x7F, 0x3C, 0x13, 0x75, 0x03, 0xE8, 0x37, 0xFA, 0x3C, 0x03, 0x74, 0x02, 0x58,
    0xCF, 0xE8, 0x20, 0xFA, 0xE9, 0xB0, 0xF9, 0x41, 0x58, 0x42, 0x58, 0x43, 0x58, 0x44, 0x58, 0x53,
    0x50, 0x42, 0x50, 0x53, 0x49, 0x44, 0x49, 0x44, 0x53, 0x45, 0x53, 0x53, 0x53, 0x43, 0x53, 0x49,
    0x50, 0x50, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4F, 0x56, 0x44, 0x4E, 0x45,
    0x49, 0x00, 0x00, 0x4E, 0x47, 0x5A, 0x52, 0x00, 0x00, 0x41, 0x43, 0x00, 0x00, 0x50, 0x45, 0x00,
    0x00, 0x43, 0x59, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4E, 0x56, 0x55, 0x50, 0x44,
    0x49, 0x00, 0x00, 0x50, 0x4C, 0x4E, 0x5A, 0x00, 0x00, 0x4E, 0x41, 0x00, 0x00, 0x50, 0x4F, 0x00,
    0x00, 0x4E, 0x43, 0x01, 0x19, 0x04, 0x10, 0x02, 0x0F, 0xFD, 0x01, 0x19, 0x04, 0x18, 0x01, 0x0B,
    0xFD, 0x06, 0x63, 0x0B, 0x07, 0x00, 0x06, 0x00, 0x02, 0x70, 0x05, 0x00, 0x04, 0xB7, 0x77, 0xCE,
    0x37, 0x0D, 0x0A, 0x0A, 0x53, 0x43, 0x50, 0x20, 0x38, 0x30, 0x38, 0x36, 0x20, 0x4D, 0x6F, 0x6E,
    0x69, 0x74, 0x6F, 0x72, 0x20, 0x31, 0x2E, 0x35, 0x0D, 0x8A, 0x5E, 0x20, 0x45, 0x72, 0x72, 0x6F,
    0x72, 0x0D, 0x8A, 0x08, 0x20, 0x88, 0x57, 0xB0, 0x01, 0xE6, 0x02, 0xB0, 0x84, 0xE6, 0x00, 0xB0,
    0x7F, 0xE6, 0x04, 0xB6, 0x21, 0xB0, 0x30, 0xE6, 0x34, 0xB9, 0xC4, 0xAA, 0xD4, 0x0A, 0xD4, 0x0A,
    0xE2, 0xFA, 0xB0, 0xD0, 0xE6, 0x30, 0xD4, 0x0A, 0xD4, 0x0A, 0xD4, 0x0A, 0xD4, 0x0A, 0x80, 0xF6,
    0x10, 0x8A, 0xC6, 0xE6, 0x34, 0xBF, 0x00, 0x02, 0xB0, 0x0F, 0xE6, 0x30, 0xE4, 0x34, 0xD0, 0xC8,
    0x73, 0xFA, 0xE4, 0x30, 0x24, 0x98, 0x75, 0xDA, 0xB0, 0x01, 0xE6, 0x32, 0x8A, 0xC6, 0x0C, 0x80,
    0xE6, 0x34, 0xB2, 0x33, 0xB0, 0x8C, 0xE6, 0x30, 0xEB, 0x02, 0xEC, 0xAA, 0xE4, 0x34, 0xD0, 0xC8,
    0x73, 0xF8, 0xE4, 0x30, 0x24, 0x9C, 0x75, 0xBA, 0xC7, 0x06, 0xB2, 0x01, 0x00, 0x00, 0xC7, 0x06,
    0xB4, 0x01, 0x00, 0x02, 0x5F, 0xE9, 0x82, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xEA, 0x00, 0x00, 0x80, 0xFF, 0x0D, 0x00, 0x68, 0x00, 0xA0, 0x01, 0x40, 0x03, 0x78, 0x04, 0xFF
};

 static int32 scp300f_mem(const int32 Addr, const int32 write, const int32 data)
{
/*  DBG_PRINT(("SCP300F: ROM %s, Addr %04x" NLP, write ? "WR" : "RD", Addr)); */
    if(write) {
        if(scp300f_info->rom_enabled)
        {
            sim_debug(ROM_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " WR ROM[0x%05x]: Cannot write to ROM.\n", PCX, Addr);
        } else {
        }
        return 0;
    } else {
        if(scp300f_info->rom_enabled)
        {
            return scp300f_rom[Addr & SCP300F_ADDR_MASK];
        } else {
            return scp300f_ram[Addr & SCP300F_ADDR_MASK];
        }
    }
}

static int32 scp300fdev(const int32 port, const int32 io, const int32 data)
{
/*    DBG_PRINT(("SCP300F: IO %s, Port %02x\n", io ? "WR" : "RD", port)); */
    if(io) {
        SCP300F_Write(port, data);
        return 0;
    } else {
        return(SCP300F_Read(port));
    }
}

#define SCP300F_MPIC_0      0x00 /* Master PIC */
#define SCP300F_MPIC_1      0x01 /* Master PIC */
#define SCP300F_SPIC_0      0x02 /* Slave PIC */
#define SCP300F_SPIC_1      0x03 /* Slave PIC */
#define SCP300F_9513_DATA   0x04 /* 9513 counter/timer Data Port */
#define SCP300F_9513_STATUS 0x05 /* 9513 counter/timer Status/Control Port */
#define SCP300F_UART_DATA   0x06 /* UART Data Register */
#define SCP300F_UART_STATUS 0x07 /* UART Status */

#define SCP300F_PIO_DATA    0x0C /* PIO Data */
#define SCP300F_PIO_STATUS  0x0D /* PIO Status */
#define SCP300F_EPROM_DIS   0x0E /* EPROM Disable */
#define SCP300F_SENSE_SW    0x0F /* Sense Switch */

extern int32 sio0d(const int32 port, const int32 io, const int32 data);
extern int32 sio0s(const int32 port, const int32 io, const int32 data);

static uint8 SCP300F_Read(const uint32 Addr)
{
    uint8 cData = 0xFF;

    switch(Addr & SCP300F_IO_MASK) {
        case SCP300F_MPIC_0:
        case SCP300F_MPIC_1:
            sim_debug(UART_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " Master 8259 DATA RD[%02x]: not implemented.\n", PCX, Addr);
            break;
        case SCP300F_SPIC_0:
        case SCP300F_SPIC_1:
            sim_debug(UART_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " Slave 8259 DATA RD[%02x]: not implemented.\n", PCX, Addr);
            break;
        case SCP300F_9513_DATA:
            sim_debug(UART_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " 9513 DATA RD[%02x]: not implemented.\n", PCX, Addr);
            break;
        case SCP300F_9513_STATUS:
            sim_debug(UART_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " 9513 STAT RD[%02x]: not implemented.\n", PCX, Addr);
            break;
        case SCP300F_UART_DATA:     /* UART is handled by the 2SIO, if this gets called, then the 2SIO was not */
        case SCP300F_UART_STATUS:   /* configured properly. */
            sim_debug(VERBOSE_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " RD[%02x]: UART not configured properly.\n", PCX, Addr);
            break;
        case SCP300F_PIO_DATA:
            sim_debug(UART_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " PIO DATA RD[%02x]: not implemented.\n", PCX, Addr);
            break;
        case SCP300F_PIO_STATUS:
            sim_debug(UART_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " PIO STATUS RD[%02x]: not implemented.\n", PCX, Addr);
            break;
        case SCP300F_EPROM_DIS:
            sim_debug(ROM_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " EPROM DIS RD: EPROM Disabled.\n", PCX);
            scp300f_info->rom_enabled = 0;
            break;
        case SCP300F_SENSE_SW:      /* Sense Switch */
            cData = scp300f_sr;
            sim_debug(VERBOSE_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " RD: Sense Switch=0x%02x\n", PCX, cData);
            break;
        default:
            sim_debug(VERBOSE_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " RD[%02x]: not Implemented.\n", PCX, Addr);
            break;
    }

    return (cData);

}

static uint8 SCP300F_Write(const uint32 Addr, uint8 cData)
{

    switch(Addr & SCP300F_IO_MASK) {
        case SCP300F_MPIC_0:
        case SCP300F_MPIC_1:
            sim_debug(UART_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " Master 8259 DATA WR[%02x]=%02x: not implemented.\n", PCX, Addr, cData);
            break;
        case SCP300F_SPIC_0:
        case SCP300F_SPIC_1:
            sim_debug(UART_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " Slave 8259 DATA WR[%02x]=%02x: not implemented.\n", PCX, Addr, cData);
            break;
        case SCP300F_9513_DATA:
            sim_debug(UART_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " 9513 DATA WR[%02x]=%02x: not implemented.\n", PCX, Addr, cData);
            break;
        case SCP300F_9513_STATUS:
            sim_debug(UART_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " 9513 STAT WR[%02x]=%02x: not implemented.\n", PCX, Addr, cData);
            break;
        case SCP300F_UART_DATA:     /* UART is handled by the 2SIO, if this gets called, then the 2SIO was not */
        case SCP300F_UART_STATUS:   /* configured properly. */
            sim_debug(UART_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " WR[%02x]: UART not configured properly.\n", PCX, Addr);
            break;
        case SCP300F_PIO_DATA:
            sim_debug(UART_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " PIO DATA WR[%02x]=%02x: not implemented.\n", PCX, Addr, cData);
            break;
        case SCP300F_PIO_STATUS:
            sim_debug(UART_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " WR[%02x]: Cannot write to PIO STATUS.\n", PCX, Addr);
            break;
        case SCP300F_EPROM_DIS:
            sim_debug(ROM_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " EPROM DIS WR: EPROM Disabled.\n", PCX);
            scp300f_info->rom_enabled = 0;
            break;
        case SCP300F_SENSE_SW:
            sim_debug(VERBOSE_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " WR[%02x]: Cannot write to SR.\n", PCX, Addr);
            break;
        default:
            sim_debug(VERBOSE_MSG, &scp300f_dev, "SCP300F: " ADDRESS_FORMAT " WR[0x%02x]=0x%02x: not Implemented.\n", PCX, Addr, cData);
            break;
    }

    return(0);
}

