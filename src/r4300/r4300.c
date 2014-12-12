/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - r4300.c                                                 *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdlib.h>
#include <string.h>

#include "api/m64p_types.h"
#include "api/callbacks.h"
#include "api/debugger.h"
#include "memory/memory.h"
#include "main/main.h"
#include "main/rom.h"

#include "r4300.h"
#include "cached_interp.h"
#include "cp0.h"
#include "cp1.h"
#include "ops.h"
#include "interupt.h"
#include "pure_interp.h"
#include "recomp.h"
#include "recomph.h"
#include "tlb.h"
#include "new_dynarec/new_dynarec.h"

#ifdef DBG
#include "debugger/dbg_types.h"
#include "debugger/debugger.h"
#endif

#if defined(COUNT_INSTR)
#include "instr_counters.h"
#endif

unsigned int r4300emu = 0;
unsigned int count_per_op = COUNT_PER_OP_DEFAULT;
int llbit, rompause;
#if NEW_DYNAREC != NEW_DYNAREC_ARM
int stop;
long long int reg[32], hi, lo;
unsigned int next_interupt;
precomp_instr *PC;
#endif
long long int local_rs;
unsigned int delay_slot, skip_jump = 0, dyna_interp = 0, last_addr;

cpu_instruction_table current_instruction_table;

void generic_jump_to(unsigned int address)
{
   if (r4300emu == CORE_PURE_INTERPRETER)
      PC->addr = address;
   else {
#ifdef NEW_DYNAREC
      if (r4300emu == CORE_DYNAREC)
         last_addr = pcaddr;
      else
         jump_to(address);
#else
      jump_to(address);
#endif
   }
}

/* this hard reset function simulates the boot-up state of the R4300 CPU */
void r4300_reset_hard(void)
{
    unsigned int i;

    // clear r4300 registers and TLB entries
    for (i = 0; i < 32; i++)
    {
        reg[i]=0;
        g_cp0_regs[i]=0;
        reg_cop1_fgr_64[i]=0;

        // --------------tlb------------------------
        tlb_e[i].mask=0;
        tlb_e[i].vpn2=0;
        tlb_e[i].g=0;
        tlb_e[i].asid=0;
        tlb_e[i].pfn_even=0;
        tlb_e[i].c_even=0;
        tlb_e[i].d_even=0;
        tlb_e[i].v_even=0;
        tlb_e[i].pfn_odd=0;
        tlb_e[i].c_odd=0;
        tlb_e[i].d_odd=0;
        tlb_e[i].v_odd=0;
        tlb_e[i].r=0;
        //tlb_e[i].check_parity_mask=0x1000;

        tlb_e[i].start_even=0;
        tlb_e[i].end_even=0;
        tlb_e[i].phys_even=0;
        tlb_e[i].start_odd=0;
        tlb_e[i].end_odd=0;
        tlb_e[i].phys_odd=0;
    }
    for (i=0; i<0x100000; i++)
    {
        tlb_LUT_r[i] = 0;
        tlb_LUT_w[i] = 0;
    }
    llbit=0;
    hi=0;
    lo=0;
    FCR0=0x511;
    FCR31=0;

    // set COP0 registers
    g_cp0_regs[CP0_RANDOM_REG] = 31;
    g_cp0_regs[CP0_STATUS_REG]= 0x34000000;
    set_fpr_pointers(g_cp0_regs[CP0_STATUS_REG]);
    g_cp0_regs[CP0_CONFIG_REG]= 0x6e463;
    g_cp0_regs[CP0_PREVID_REG] = 0xb00;
    g_cp0_regs[CP0_COUNT_REG] = 0x5000;
    g_cp0_regs[CP0_CAUSE_REG] = 0x5C;
    g_cp0_regs[CP0_CONTEXT_REG] = 0x7FFFF0;
    g_cp0_regs[CP0_EPC_REG] = 0xFFFFFFFF;
    g_cp0_regs[CP0_BADVADDR_REG] = 0xFFFFFFFF;
    g_cp0_regs[CP0_ERROREPC_REG] = 0xFFFFFFFF;
   
    rounding_mode = 0x33F;
}


static unsigned int get_tv_type(void)
{
    switch(ROM_PARAMS.systemtype)
    {
    default:
    case SYSTEM_NTSC: return 1;
    case SYSTEM_PAL: return 0;
    case SYSTEM_MPAL: return 2;
    }
}

static unsigned int get_cic_seed(void)
{
    switch(CIC_Chip)
    {
        default:
            DebugMessage(M64MSG_WARNING, "Unknown CIC (%d)! using CIC 6102.", CIC_Chip);
        case 1:
        case 2:
            return 0x3f;
        case 3:
            return 0x78;
        case 5:
            return 0x91;
        case 6:
            return 0x85;
    }
}

/* Simulates end result of PIFBootROM execution */
void r4300_reset_soft(void)
{
    unsigned int rom_type = 0;              /* 0:Cart, 1:DD */
    unsigned int reset_type = 0;            /* 0:ColdReset, 1:NMI */
    unsigned int s7 = 0;                    /* ??? */
    unsigned int tv_type = get_tv_type();   /* 0:PAL, 1:NTSC, 2:MPAL */
    unsigned int cic_seed = get_cic_seed();
    uint32_t bsd_dom1_config = *(uint32_t*)rom;

    g_cp0_regs[CP0_STATUS_REG] = 0x34000000;
    g_cp0_regs[CP0_CONFIG_REG] = 0x0006e463;

    sp_register.sp_status_reg = 1;
    rsp_register.rsp_pc = 0;

    pi_register.pi_bsd_dom1_lat_reg = (bsd_dom1_config      ) & 0xff;
    pi_register.pi_bsd_dom1_pwd_reg = (bsd_dom1_config >>  8) & 0xff;
    pi_register.pi_bsd_dom1_pgs_reg = (bsd_dom1_config >> 16) & 0x0f;
    pi_register.pi_bsd_dom1_rls_reg = (bsd_dom1_config >> 20) & 0x03;
    pi_register.read_pi_status_reg = 0;

    ai_register.ai_dram_addr = 0;
    ai_register.ai_len = 0;

    vi_register.vi_v_intr = 1023;
    vi_register.vi_current = 0;
    vi_register.vi_h_start = 0;

    MI_register.mi_intr_reg &= ~(0x10 | 0x8 | 0x4 | 0x1);

    memcpy((unsigned char*)SP_DMEM+0x40, rom+0x40, 0xfc0);

    reg[19] = rom_type;     /* s3 */
    reg[20] = tv_type;      /* s4 */
    reg[21] = reset_type;   /* s5 */
    reg[22] = cic_seed;     /* s6 */
    reg[23] = s7;           /* s7 */

    /* required by CIC x105 */
    SP_IMEM[0] = 0x3c0dbfc0;
    SP_IMEM[1] = 0x8da807fc;
    SP_IMEM[2] = 0x25ad07c0;
    SP_IMEM[3] = 0x31080080;
    SP_IMEM[4] = 0x5500fffc;
    SP_IMEM[5] = 0x3c0dbfc0;
    SP_IMEM[6] = 0x8da80024;
    SP_IMEM[7] = 0x3c0bb000;

    /* required by CIC x105 */
    reg[11] = 0xffffffffa4000040ULL; /* t3 */
    reg[29] = 0xffffffffa4001ff0ULL; /* sp */
    reg[31] = 0xffffffffa4001550ULL; /* ra */

    /* ready to execute IPL3 */
}

#if !defined(NO_ASM)
static void dynarec_setup_code(void)
{
   // The dynarec jumps here after we call dyna_start and it prepares
   // Here we need to prepare the initial code block and jump to it
   jump_to(0xa4000040);

   // Prevent segfault on failed jump_to
   if (!actual->block || !actual->code)
      dyna_stop();
}
#endif

void r4300_execute(void)
{
#if (defined(DYNAREC) && defined(PROFILE_R4300))
    unsigned int i;
#endif

    current_instruction_table = cached_interpreter_table;

    delay_slot=0;
    stop = 0;
    rompause = 0;

    /* clear instruction counters */
#if defined(COUNT_INSTR)
    memset(instr_count, 0, 131*sizeof(instr_count[0]));
#endif

    last_addr = 0xa4000040;
    next_interupt = 624999;
    init_interupt();

    if (r4300emu == CORE_PURE_INTERPRETER)
    {
        DebugMessage(M64MSG_INFO, "Starting R4300 emulator: Pure Interpreter");
        r4300emu = CORE_PURE_INTERPRETER;
        pure_interpreter();
    }
#if defined(DYNAREC)
    else if (r4300emu >= 2)
    {
        DebugMessage(M64MSG_INFO, "Starting R4300 emulator: Dynamic Recompiler");
        r4300emu = CORE_DYNAREC;
        init_blocks();

#ifdef NEW_DYNAREC
        new_dynarec_init();
        new_dyna_start();
        new_dynarec_cleanup();
#else
        dyna_start(dynarec_setup_code);
        PC++;
#endif
#if defined(PROFILE_R4300)
        pfProfile = fopen("instructionaddrs.dat", "ab");
        for (i=0; i<0x100000; i++)
            if (invalid_code[i] == 0 && blocks[i] != NULL && blocks[i]->code != NULL && blocks[i]->block != NULL)
            {
                unsigned char *x86addr;
                int mipsop;
                // store final code length for this block
                mipsop = -1; /* -1 == end of x86 code block */
                x86addr = blocks[i]->code + blocks[i]->code_length;
                if (fwrite(&mipsop, 1, 4, pfProfile) != 4 ||
                    fwrite(&x86addr, 1, sizeof(char *), pfProfile) != sizeof(char *))
                    DebugMessage(M64MSG_ERROR, "Error writing R4300 instruction address profiling data");
            }
        fclose(pfProfile);
        pfProfile = NULL;
#endif
        free_blocks();
    }
#endif
    else /* if (r4300emu == CORE_INTERPRETER) */
    {
        DebugMessage(M64MSG_INFO, "Starting R4300 emulator: Cached Interpreter");
        r4300emu = CORE_INTERPRETER;
        init_blocks();
        jump_to(0xa4000040);

        /* Prevent segfault on failed jump_to */
        if (!actual->block)
            return;

        last_addr = PC->addr;
        while (!stop)
        {
#ifdef COMPARE_CORE
            if (PC->ops == cached_interpreter_table.FIN_BLOCK && (PC->addr < 0x80000000 || PC->addr >= 0xc0000000))
                virtual_to_physical_address(PC->addr, 2);
            CoreCompareCallback();
#endif
#ifdef DBG
            if (g_DebuggerActive) update_debugger(PC->addr);
#endif
            PC->ops();
        }

        free_blocks();
    }

    DebugMessage(M64MSG_INFO, "R4300 emulator finished.");

    /* print instruction counts */
#if defined(COUNT_INSTR)
    if (r4300emu == CORE_DYNAREC)
        instr_counters_print();
#endif
}
