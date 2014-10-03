/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - interupt.c                                              *
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

#include <string.h>

#include <SDL.h>

#define M64P_CORE_PROTOTYPES 1
#include "api/m64p_types.h"
#include "api/callbacks.h"
#include "api/m64p_vidext.h"
#include "api/vidext.h"
#include "memory/memory.h"
#include "main/rom.h"
#include "main/main.h"
#include "main/profile.h"
#include "main/savestates.h"
#include "main/cheat.h"
#include "osd/osd.h"
#include "plugin/plugin.h"

#include "interupt.h"
#include "r4300.h"
#include "cached_interp.h"
#include "cp0.h"
#include "exception.h"
#include "reset.h"
#include "new_dynarec/new_dynarec.h"

#ifdef WITH_LIRC
#include "main/lirc.h"
#endif

unsigned int next_vi;
int vi_field=0;
static int vi_counter=0;

int interupt_unsafe_state = 0;

struct interrupt_event
{
    int type;
    unsigned int count;
};


/***************************************************************************
 * Pool of Single Linked List Nodes
 **************************************************************************/
#define POOL_CAPACITY 16

struct node
{
    struct interrupt_event data;
    struct node *next;
};

struct pool
{
    struct node nodes[POOL_CAPACITY];
    struct node* stack[POOL_CAPACITY];
    size_t index;
};

static struct node* alloc_node(struct pool* p);
static void free_node(struct pool* p, struct node* node);
static void clear_pool(struct pool* p);


/* node allocation/deallocation on a given pool */
static struct node* alloc_node(struct pool* p)
{
    /* return NULL if pool is too small */
    if (p->index >= POOL_CAPACITY)
        return NULL;

    return p->stack[p->index++];
}

static void free_node(struct pool* p, struct node* node)
{
    if (p->index == 0 || node == NULL)
        return;

    p->stack[--p->index] = node;
}

/* release all nodes */
static void clear_pool(struct pool* p)
{
    size_t i;

    for(i = 0; i < POOL_CAPACITY; ++i)
        p->stack[i] = &p->nodes[i];

    p->index = 0;
}

/***************************************************************************
 * Interrupt Queue
 **************************************************************************/

struct interrupt_queue
{
    struct pool pool;
    struct node* first;
};

static struct interrupt_queue q;


static void clear_queue(void)
{
    q.first = NULL;
    clear_pool(&q.pool);
}


static int SPECIAL_done = 0;

static int before_event(unsigned int evt1, unsigned int evt2, int type2)
{
    if(evt1 - g_cp0_regs[CP0_COUNT_REG] < 0x80000000)
    {
        if(evt2 - g_cp0_regs[CP0_COUNT_REG] < 0x80000000)
        {
            if((evt1 - g_cp0_regs[CP0_COUNT_REG]) < (evt2 - g_cp0_regs[CP0_COUNT_REG])) return 1;
            else return 0;
        }
        else
        {
            if((g_cp0_regs[CP0_COUNT_REG] - evt2) < 0x10000000)
            {
                switch(type2)
                {
                    case SPECIAL_INT:
                        if(SPECIAL_done) return 1;
                        else return 0;
                        break;
                    default:
                        return 0;
                }
            }
            else return 1;
        }
    }
    else return 0;
}

void add_interupt_event(int type, unsigned int delay)
{
    add_interupt_event_count(type, g_cp0_regs[CP0_COUNT_REG] + delay);
}

void add_interupt_event_count(int type, unsigned int count)
{
    struct node* event;
    struct node* e;
    int special;

    special = (type == SPECIAL_INT);
   
    if(g_cp0_regs[CP0_COUNT_REG] > 0x80000000) SPECIAL_done = 0;
   
    if (get_event(type)) {
        DebugMessage(M64MSG_WARNING, "two events of type 0x%x in interrupt queue", type);
        /* FIXME: hack-fix for freezing in Perfect Dark
         * http://code.google.com/p/mupen64plus/issues/detail?id=553
         * https://github.com/mupen64plus-ae/mupen64plus-ae/commit/802d8f81d46705d64694d7a34010dc5f35787c7d
         */
        return;
    }

    event = alloc_node(&q.pool);
    if (event == NULL)
    {
        DebugMessage(M64MSG_ERROR, "Failed to allocate node for new interrupt event");
        return;
    }

    event->data.count = count;
    event->data.type = type;

    if (q.first == NULL)
    {
        q.first = event;
        event->next = NULL;
        next_interupt = q.first->data.count;
    }
    else if (before_event(count, q.first->data.count, q.first->data.type) && !special)
    {
        event->next = q.first;
        q.first = event;
        next_interupt = q.first->data.count;
    }
    else
    {
        for(e = q.first;
            e->next != NULL &&
            (!before_event(count, e->next->data.count, e->next->data.type) || special);
            e = e->next);

        if (e->next == NULL)
        {
            e->next = event;
            event->next = NULL;
        }
        else
        {
            if (!special)
                for(; e->next != NULL && e->next->data.count == count; e = e->next);

            event->next = e->next;
            e->next = event;
        }
    }
}

static void remove_interupt_event(void)
{
    struct node* e;

    if (q.first->data.type == SPECIAL_INT)
        SPECIAL_done = 1;

    e = q.first;
    q.first = e->next;
    free_node(&q.pool, e);

    next_interupt = (q.first != NULL
         && (q.first->data.count > g_cp0_regs[CP0_COUNT_REG]
         || (g_cp0_regs[CP0_COUNT_REG] - q.first->data.count) < 0x80000000))
        ? q.first->data.count
        : 0;
}

unsigned int get_event(int type)
{
    struct node* e = q.first;

    if (e == NULL)
        return 0;

    if (e->data.type == type)
        return e->data.count;

    for(; e->next != NULL && e->next->data.type != type; e = e->next);

    return (e->next != NULL)
        ? e->next->data.count
        : 0;
}

int get_next_event_type(void)
{
    return (q.first == NULL)
        ? 0
        : q.first->data.type;
}

void remove_event(int type)
{
    struct node* to_del;
    struct node* e = q.first;

    if (e == NULL)
        return;

    if (e->data.type == type)
    {
        q.first = e->next;
        free_node(&q.pool, e);
    }
    else
    {
        for(; e->next != NULL && e->next->data.type != type; e = e->next);

        if (e->next != NULL)
        {
            to_del = e->next;
            e->next = to_del->next;
            free_node(&q.pool, to_del);
        }
    }
}

void translate_event_queue(unsigned int base)
{
    struct node* e;

    remove_event(COMPARE_INT);
    remove_event(SPECIAL_INT);

    for(e = q.first; e != NULL; e = e->next)
    {
        e->data.count = (e->data.count - g_cp0_regs[CP0_COUNT_REG]) + base;
    }
    add_interupt_event_count(COMPARE_INT, g_cp0_regs[CP0_COMPARE_REG]);
    add_interupt_event_count(SPECIAL_INT, 0);
}

int save_eventqueue_infos(char *buf)
{
    int len;
    struct node* e;

    len = 0;

    for(e = q.first; e != NULL; e = e->next)
    {
        memcpy(buf + len    , &e->data.type , 4);
        memcpy(buf + len + 4, &e->data.count, 4);
        len += 8;
    }

    *((unsigned int*)&buf[len]) = 0xFFFFFFFF;
    return len+4;
}

void load_eventqueue_infos(char *buf)
{
    int len = 0;
    clear_queue();
    while (*((unsigned int*)&buf[len]) != 0xFFFFFFFF)
    {
        int type = *((unsigned int*)&buf[len]);
        unsigned int count = *((unsigned int*)&buf[len+4]);
        add_interupt_event_count(type, count);
        len += 8;
    }
}

void init_interupt(void)
{
    SPECIAL_done = 1;
    next_vi = next_interupt = 5000;
    vi_register.vi_delay = next_vi;
    vi_field = 0;

    clear_queue();
    add_interupt_event_count(VI_INT, next_vi);
    add_interupt_event_count(SPECIAL_INT, 0);
}

void check_interupt(void)
{
    struct node* event;

    if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
        g_cp0_regs[CP0_CAUSE_REG] = (g_cp0_regs[CP0_CAUSE_REG] | 0x400) & 0xFFFFFF83;
    else
        g_cp0_regs[CP0_CAUSE_REG] &= ~0x400;
    if ((g_cp0_regs[CP0_STATUS_REG] & 7) != 1) return;
    if (g_cp0_regs[CP0_STATUS_REG] & g_cp0_regs[CP0_CAUSE_REG] & 0xFF00)
    {
        event = alloc_node(&q.pool);

        if (event == NULL)
        {
            DebugMessage(M64MSG_ERROR, "Failed to allocate node for new interrupt event");
            return;
        }

        event->data.count = next_interupt = g_cp0_regs[CP0_COUNT_REG];
        event->data.type = CHECK_INT;

        if (q.first == NULL)
        {
            q.first = event;
            event->next = NULL;
        }
        else
        {
            event->next = q.first;
            q.first = event;

        }
    }
}

void gen_interupt(void)
{
    if (stop == 1)
    {
        vi_counter = 0; // debug
        dyna_stop();
    }

    if (!interupt_unsafe_state)
    {
        if (savestates_get_job() == savestates_job_load)
        {
            savestates_load();
            return;
        }

        if (reset_hard_job)
        {
            reset_hard();
            reset_hard_job = 0;
            return;
        }
    }
   
    if (skip_jump)
    {
        unsigned int dest = skip_jump;
        skip_jump = 0;

        next_interupt = (q.first->data.count > g_cp0_regs[CP0_COUNT_REG]
                || (g_cp0_regs[CP0_COUNT_REG] - q.first->data.count) < 0x80000000)
            ? q.first->data.count
            : 0;

        last_addr = dest;
        generic_jump_to(dest);
        return;
    } 

    switch(q.first->data.type)
    {
        case SPECIAL_INT:
            if (g_cp0_regs[CP0_COUNT_REG] > 0x10000000) return;
            remove_interupt_event();
            add_interupt_event_count(SPECIAL_INT, 0);
            return;
            break;
        case VI_INT:
            if(vi_counter < 60)
            {
                if (vi_counter == 0)
                    cheat_apply_cheats(ENTRY_BOOT);
                vi_counter++;
            }
            else
            {
                cheat_apply_cheats(ENTRY_VI);
            }
            gfx.updateScreen();
#ifdef WITH_LIRC
            lircCheckInput();
#endif
            SDL_PumpEvents();

            timed_sections_refresh();

            // if paused, poll for input events
            if(rompause)
            {
                osd_render();  // draw Paused message in case gfx.updateScreen didn't do it
                VidExt_GL_SwapBuffers();
                while(rompause)
                {
                    SDL_Delay(10);
                    SDL_PumpEvents();
#ifdef WITH_LIRC
                    lircCheckInput();
#endif //WITH_LIRC
                }
            }

            new_vi();
            if (vi_register.vi_v_sync == 0) vi_register.vi_delay = 500000;
            else vi_register.vi_delay = ((vi_register.vi_v_sync + 1)*1500);
            next_vi += vi_register.vi_delay;
            if (vi_register.vi_status&0x40) vi_field=1-vi_field;
            else vi_field=0;

            remove_interupt_event();
            add_interupt_event_count(VI_INT, next_vi);
    
            MI_register.mi_intr_reg |= 0x08;
            if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                g_cp0_regs[CP0_CAUSE_REG] = (g_cp0_regs[CP0_CAUSE_REG] | 0x400) & 0xFFFFFF83;
            else
                return;
            if ((g_cp0_regs[CP0_STATUS_REG] & 7) != 1) return;
            if (!(g_cp0_regs[CP0_STATUS_REG] & g_cp0_regs[CP0_CAUSE_REG] & 0xFF00)) return;
            break;
    
        case COMPARE_INT:
            remove_interupt_event();
            g_cp0_regs[CP0_COUNT_REG]+=count_per_op;
            add_interupt_event_count(COMPARE_INT, g_cp0_regs[CP0_COMPARE_REG]);
            g_cp0_regs[CP0_COUNT_REG]-=count_per_op;
    
            g_cp0_regs[CP0_CAUSE_REG] = (g_cp0_regs[CP0_CAUSE_REG] | 0x8000) & 0xFFFFFF83;
            if ((g_cp0_regs[CP0_STATUS_REG] & 7) != 1) return;
            if (!(g_cp0_regs[CP0_STATUS_REG] & g_cp0_regs[CP0_CAUSE_REG] & 0xFF00)) return;
            break;
    
        case CHECK_INT:
            remove_interupt_event();
            break;
    
        case SI_INT:
#ifdef WITH_LIRC
            lircCheckInput();
#endif //WITH_LIRC
            SDL_PumpEvents();
            PIF_RAMb[0x3F] = 0x0;
            remove_interupt_event();
            MI_register.mi_intr_reg |= 0x02;
            si_register.si_stat |= 0x1000;
            if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                g_cp0_regs[CP0_CAUSE_REG] = (g_cp0_regs[CP0_CAUSE_REG] | 0x400) & 0xFFFFFF83;
            else
                return;
            if ((g_cp0_regs[CP0_STATUS_REG] & 7) != 1) return;
            if (!(g_cp0_regs[CP0_STATUS_REG] & g_cp0_regs[CP0_CAUSE_REG] & 0xFF00)) return;
            break;
    
        case PI_INT:
            remove_interupt_event();
            MI_register.mi_intr_reg |= 0x10;
            pi_register.read_pi_status_reg &= ~3;
            if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                g_cp0_regs[CP0_CAUSE_REG] = (g_cp0_regs[CP0_CAUSE_REG] | 0x400) & 0xFFFFFF83;
            else
                return;
            if ((g_cp0_regs[CP0_STATUS_REG] & 7) != 1) return;
            if (!(g_cp0_regs[CP0_STATUS_REG] & g_cp0_regs[CP0_CAUSE_REG] & 0xFF00)) return;
            break;
    
        case AI_INT:
            if (ai_register.ai_status & 0x80000000) // full
            {
                unsigned int ai_event = get_event(AI_INT);
                remove_interupt_event();
                ai_register.ai_status &= ~0x80000000;
                ai_register.current_delay = ai_register.next_delay;
                ai_register.current_len = ai_register.next_len;
                add_interupt_event_count(AI_INT, ai_event+ai_register.next_delay);
         
                MI_register.mi_intr_reg |= 0x04;
                if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                    g_cp0_regs[CP0_CAUSE_REG] = (g_cp0_regs[CP0_CAUSE_REG] | 0x400) & 0xFFFFFF83;
                else
                    return;
                if ((g_cp0_regs[CP0_STATUS_REG] & 7) != 1) return;
                if (!(g_cp0_regs[CP0_STATUS_REG] & g_cp0_regs[CP0_CAUSE_REG] & 0xFF00)) return;
            }
            else
            {
                remove_interupt_event();
                ai_register.ai_status &= ~0x40000000;

                //-------
                MI_register.mi_intr_reg |= 0x04;
                if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                    g_cp0_regs[CP0_CAUSE_REG] = (g_cp0_regs[CP0_CAUSE_REG] | 0x400) & 0xFFFFFF83;
                else
                    return;
                if ((g_cp0_regs[CP0_STATUS_REG] & 7) != 1) return;
                if (!(g_cp0_regs[CP0_STATUS_REG] & g_cp0_regs[CP0_CAUSE_REG] & 0xFF00)) return;
            }
            break;

        case SP_INT:
            remove_interupt_event();
            sp_register.sp_status_reg |= 0x203;
            // sp_register.sp_status_reg |= 0x303;
    
            if (!(sp_register.sp_status_reg & 0x40)) return; // !intr_on_break
            MI_register.mi_intr_reg |= 0x01;
            if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                g_cp0_regs[CP0_CAUSE_REG] = (g_cp0_regs[CP0_CAUSE_REG] | 0x400) & 0xFFFFFF83;
            else
                return;
            if ((g_cp0_regs[CP0_STATUS_REG] & 7) != 1) return;
            if (!(g_cp0_regs[CP0_STATUS_REG] & g_cp0_regs[CP0_CAUSE_REG] & 0xFF00)) return;
            break;
    
        case DP_INT:
            remove_interupt_event();
            dpc_register.dpc_status &= ~2;
            dpc_register.dpc_status |= 0x81;
            MI_register.mi_intr_reg |= 0x20;
            if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                g_cp0_regs[CP0_CAUSE_REG] = (g_cp0_regs[CP0_CAUSE_REG] | 0x400) & 0xFFFFFF83;
            else
                return;
            if ((g_cp0_regs[CP0_STATUS_REG] & 7) != 1) return;
            if (!(g_cp0_regs[CP0_STATUS_REG] & g_cp0_regs[CP0_CAUSE_REG] & 0xFF00)) return;
            break;

        case HW2_INT:
            // Hardware Interrupt 2 -- remove interrupt event from queue
            remove_interupt_event();
            // setup r4300 Status flags: reset TS, and SR, set IM2
            g_cp0_regs[CP0_STATUS_REG] = (g_cp0_regs[CP0_STATUS_REG] & ~0x00380000) | 0x1000;
            g_cp0_regs[CP0_CAUSE_REG] = (g_cp0_regs[CP0_CAUSE_REG] | 0x1000) & 0xFFFFFF83;
            /* the exception_general() call below will jump to the interrupt vector (0x80000180) and setup the
             * interpreter or dynarec
             */
            break;

        case NMI_INT:
            // Non Maskable Interrupt -- remove interrupt event from queue
            remove_interupt_event();
            // setup r4300 Status flags: reset TS and SR, set BEV, ERL, and SR
            g_cp0_regs[CP0_STATUS_REG] = (g_cp0_regs[CP0_STATUS_REG] & ~0x00380000) | 0x00500004;
            g_cp0_regs[CP0_CAUSE_REG]  = 0x00000000;
            // simulate the soft reset code which would run from the PIF ROM
            r4300_reset_soft();
            // clear all interrupts, reset interrupt counters back to 0
            g_cp0_regs[CP0_COUNT_REG] = 0;
            vi_counter = 0;
            init_interupt();
            // clear the audio status register so that subsequent write_ai() calls will work properly
            ai_register.ai_status = 0;
            // set ErrorEPC with the last instruction address
            g_cp0_regs[CP0_ERROREPC_REG] = PC->addr;
            // reset the r4300 internal state
            if (r4300emu != CORE_PURE_INTERPRETER)
            {
                // clear all the compiled instruction blocks and re-initialize
                free_blocks();
                init_blocks();
            }
            // adjust ErrorEPC if we were in a delay slot, and clear the delay_slot and dyna_interp flags
            if(delay_slot==1 || delay_slot==3)
            {
                g_cp0_regs[CP0_ERROREPC_REG]-=4;
            }
            delay_slot = 0;
            dyna_interp = 0;
            // set next instruction address to reset vector
            last_addr = 0xa4000040;
            generic_jump_to(0xa4000040);
            return;

        default:
            DebugMessage(M64MSG_ERROR, "Unknown interrupt queue event type %.8X.", q.first->data.type);
            remove_interupt_event();
            break;
    }

#ifdef NEW_DYNAREC
    if (r4300emu == CORE_DYNAREC) {
        g_cp0_regs[CP0_EPC_REG] = pcaddr;
        pcaddr = 0x80000180;
        g_cp0_regs[CP0_STATUS_REG] |= 2;
        g_cp0_regs[CP0_CAUSE_REG] &= 0x7FFFFFFF;
        pending_exception=1;
    } else {
        exception_general();
    }
#else
    exception_general();
#endif

    if (!interupt_unsafe_state)
    {
        if (savestates_get_job() == savestates_job_save)
        {
            savestates_save();
            return;
        }
    }
}

