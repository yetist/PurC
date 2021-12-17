/**
 * @file interpreter.h
 * @author Xu Xiaohong
 * @date 2021/11/18
 * @brief The internal interfaces for interpreter
 *
 * Copyright (C) 2021 FMSoft <https://www.fmsoft.cn>
 *
 * This file is a part of PurC (short for Purring Cat), an HVML interpreter.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef PURC_PRIVATE_INTERPRETER_H
#define PURC_PRIVATE_INTERPRETER_H

#include "purc.h"

#include "purc-macros.h"
#include "purc-variant.h"

#include "private/debug.h"
#include "private/errors.h"
#include "private/list.h"
#include "private/vdom.h"

struct pcintr_stack;
typedef struct pcintr_stack pcintr_stack;
typedef struct pcintr_stack *pcintr_stack_t;

struct pcintr_stack_frame;
typedef struct pcintr_stack_frame pcintr_stack_frame;
typedef struct pcintr_stack_frame *pcintr_stack_frame_t;

enum pcintr_stack_state {
    STACK_STATE_WAITING    = 0x01,
    STACK_STATE_TERMINATED = 0x02,
    /* STACK_STATE_PAUSED     = 0x10, */
};

#define pcintr_stack_is_waiting(stack)     \
    (stack->state & STACK_STATE_WAITING)
#define pcintr_stack_is_terminated(stack)  \
    (stack->state & STACK_STATE_TERMINATED)
#define pcintr_stack_is_paused(stack)      \
    (stack->state & STACK_STATE_PAUSED)

enum pcintr_coroutine_state {
    CO_STATE_READY,
    CO_STATE_RUN,
    CO_STATE_WAIT,
    CO_STATE_TERMINATED,
    /* STATE_PAUSED, */
};

struct pcintr_coroutine;
typedef struct pcintr_coroutine pcintr_coroutine;
typedef struct pcintr_coroutine *pcintr_coroutine_t;

struct pcintr_coroutine {
    struct list_head            node;

    struct pcintr_stack        *stack;

    enum pcintr_coroutine_state state;
    int                         waits;
};

struct pcintr_stack {
    struct list_head frames;

    // the number of stack frames.
    size_t nr_frames;

    // the pointer to the vDOM tree.
    purc_vdom_t vdom;

    // the returned variant
    purc_variant_t ret_var;

    // executing state
    uint32_t        error:1;
    uint32_t        except:1;
    /* uint32_t        paused:1; */

    uint32_t        state;

    // error or except info
    purc_atom_t     error_except;
    purc_variant_t  err_except_info;

    // executing statistics
    struct timespec time_executed;
    struct timespec time_idle;
    size_t          peak_mem_use;
    size_t          peak_nr_variants;

    struct pcintr_coroutine        co;
};

enum purc_symbol_var {
    PURC_SYMBOL_VAR_QUESTION_MARK = 0,  // ?
    PURC_SYMBOL_VAR_AT_SIGN,            // @
    PURC_SYMBOL_VAR_NUMBER_SIGN,        // #
    PURC_SYMBOL_VAR_ASTERISK,           // *
    PURC_SYMBOL_VAR_COLON,              // :
    PURC_SYMBOL_VAR_AMPERSAND,          // &
    PURC_SYMBOL_VAR_PERCENT_SIGN,       // %

    PURC_SYMBOL_VAR_MAX
};

struct pcintr_element_ops {
    // called after pushed
    void (*after_pushed) (pcintr_coroutine_t co,
            struct pcintr_stack_frame *frame);

    // called on popping
    void (*on_popping) (pcintr_coroutine_t co,
            struct pcintr_stack_frame *frame);

    // called to rerun
    void (*rerun) (pcintr_coroutine_t co,
            struct pcintr_stack_frame *frame);

    // called after executed
    void (*select_child) (pcintr_coroutine_t co,
            struct pcintr_stack_frame *frame);
};

enum pcintr_stack_frame_next_step {
    NEXT_STEP_AFTER_PUSHED = 0,
    NEXT_STEP_ON_POPPING,
    NEXT_STEP_RERUN,
    NEXT_STEP_SELECT_CHILD,
    NEXT_STEP_CUSTOMIZED,
};

struct pcintr_stack_frame {
    // pointers to sibling frames.
    struct list_head node;

    // the current scope.
    pcvdom_element_t scope;

    // the current execution position.
    pcvdom_element_t pos;

    // the symbolized variables
    purc_variant_t symbol_vars[PURC_SYMBOL_VAR_MAX];

    // all attribute variants are managed by a map (attribute name -> variant).
    purc_variant_t attr_vars;

    // the evaluated content variant
    purc_variant_t ctnt_var;

    // all intermediate variants are managed by an array.
    purc_variant_t mid_vars;

    struct pcintr_element_ops ops;

    // context for current action
    void *ctxt;
    int   next_step;
};

struct pcintr_dynamic_args {
    const char                    *name;
    purc_dvariant_method           getter;
    purc_dvariant_method           setter;
};

struct pcinst;

PCA_EXTERN_C_BEGIN

void pcintr_stack_init_once(void) WTF_INTERNAL;
void pcintr_stack_init_instance(struct pcinst* inst) WTF_INTERNAL;
void pcintr_stack_cleanup_instance(struct pcinst* inst) WTF_INTERNAL;
pcintr_stack_t purc_get_stack (void);
struct pcintr_stack_frame*
pcintr_stack_get_bottom_frame(pcintr_stack_t stack);
struct pcintr_stack_frame*
pcintr_stack_frame_get_parent(struct pcintr_stack_frame *frame);
void
pop_stack_frame(pcintr_stack_t stack);
struct pcintr_stack_frame*
push_stack_frame(pcintr_stack_t stack);

struct pcintr_element_ops*
pcintr_get_element_ops(pcvdom_element_t element);

purc_variant_t
pcintr_make_object_of_dynamic_variants(size_t nr_args,
    struct pcintr_dynamic_args *args);

bool
pcintr_bind_buildin_variable(struct pcvdom_document* doc, const char* name,
        purc_variant_t variant);

bool
pcintr_unbind_buildin_variable(struct pcvdom_document* doc,
        const char* name);

bool
pcintr_bind_scope_variable(pcvdom_element_t elem, const char* name,
        purc_variant_t variant);

bool
pcintr_unbind_scope_variable(pcvdom_element_t elem, const char* name);

purc_variant_t
pcintr_find_named_var(pcintr_stack_t stack, const char* name);

purc_variant_t
pcintr_get_symbolized_var (pcintr_stack_t stack, unsigned int number,
        char symbol);

purc_variant_t
pcintr_get_numbered_var (pcintr_stack_t stack, unsigned int number);

PCA_EXTERN_C_END

#endif  /* PURC_PRIVATE_INTERPRETER_H */

