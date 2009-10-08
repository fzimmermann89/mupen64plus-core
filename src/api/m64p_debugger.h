/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-core - m64p_debugger.h                                    *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2009 Richard Goedeken                                   *
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

/* This header file defines typedefs for function pointers to Core Debugger
 * functions.
 */

#if !defined(M64P_DEBUGGER_H)
#define M64P_DEBUGGER_H

#include "m64p_types.h"

/* DebugSetCallbacks()
 *
 * This function is called by the front-end to supply debugger callback
 * function pointers. If debugger is enabled and then later disabled within the
 * UI, this function may be called with NULL pointers in order to disable the
 * callbacks. 
 */
typedef m64p_error (*ptr_DebugSetCallbacks)(void (*)(void), void (*)(unsigned int), void (*)(void));

/* DebugSetRunState()
 *
 * This function sets the run state of the R4300 CPU emulator.
 */
typedef m64p_error (*ptr_DebugSetRunState)(int);

/* DebugGetState()
 *
 * This function reads and returns a debugger state variable, which are
 * enumerated in m64p_types.h. 
 */
typedef int (*ptr_DebugGetState)(m64p_dbg_state);

/* DebugStep()
 *
 * This function signals the debugger to advance one instruction when in the
 * stepping mode.
 */
typedef m64p_error (*ptr_DebugStep)(void);

/* DebugDecodeOp()
 *
 * This is a helper function for the debugger front-end. This instruction takes
 * a PC value and an R4300 instruction opcode and writes the disassembled
 * instruction mnemonic and arguments into character buffers. This is intended
 * to be used to display disassembled code. 
 */
typedef void (*ptr_DebugDecodeOp)(unsigned int, char *, char *, int);

/* DebugMemGetRecompInfo()
 *
 * This function is used by the front-end to retrieve disassembly information
 * about recompiled code. For example, the dynamic recompiler may take a single
 * R4300 instruction and compile it into 10 x86 instructions. This function may
 * then be used to retrieve the disassembled code of the 10 x86 instructions.
 */
typedef void * (ptr_DebugMemGetRecompInfo)(m64p_dbg_mem_info, unsigned int, int);

/* DebugMemGetMemInfo()
 *
 * This function returns an integer value regarding the memory location address,
 * corresponding to the information requested by mem_info_type, which is a type
 * enumerated in m64p_types.h.
 */
typedef int (*ptr_DebugMemGetMemInfo)(m64p_dbg_mem_info, unsigned int);

/* DebugMemGetPointer()
 *
 * This function returns a memory pointer (in x86 memory space) to a block of
 * emulated N64 memory. This may be used to retrieve a pointer to a special N64
 * block (such as the serial, video, or audio registers) or the RDRAM.
 */
typedef void * (ptr_DebugMemGetPointer)(m64p_dbg_memptr_type);

/* DebugMemRead**()
 *
 * These functions retrieve a value from the emulated N64 memory. The returned
 * value will be correctly byte-swapped for the host architecture. 
 */
typedef unsigned long long (*ptr_DebugMemRead64)(unsigned int);
typedef unsigned int 	   (*ptr_DebugMemRead32)(unsigned int);
typedef unsigned short 	   (*ptr_DebugMemRead16)(unsigned int);
typedef unsigned char 	   (*ptr_DebugMemRead8)(unsigned int);

/* DebugMemWrite**()
 *
 * These functions write a value into the emulated N64 memory. The given value
 * will be correctly byte-swapped before storage. 
 */
typedef void (*ptr_DebugMemWrite64)(unsigned int, unsigned long long);
typedef void (*ptr_DebugMemWrite32)(unsigned int, unsigned int);
typedef void (*ptr_DebugMemWrite16)(unsigned int, unsigned short);
typedef void (*ptr_DebugMemWrite8)(unsigned int, unsigned char);

/* DebugGetCPUDataPtr()
 *
 * This function returns a memory pointer (in x86 memory space) to a specific
 * register in the emulated R4300 CPU.
 */
typedef void * (*ptr_DebugGetCPUDataPtr)(m64p_dbg_cpu_data);

/* DebugBreakpointLookup()
 *
 * This function searches through all current breakpoints in the debugger to
 * find one that matches the given input parameters. If a matching breakpoint
 * is found, the index number is returned. If no breakpoints are found, -1 is
 * returned. 
 */
typedef int (*ptr_DebugBreakpointLookup)(unsigned int, unsigned int, unsigned int);

/* DebugBreakpointCommand()
 *
 * This function is used to process common breakpoint commands, such as adding,
 * removing, or searching the breakpoints. The meanings of the index and ptr
 * input parameters vary by command.
 */
typedef int (*ptr_DebugBreakpointCommand)(m64p_dbg_bkp_command, unsigned int, void *);

#endif /* #define M64P_DEBUGGER_H */

