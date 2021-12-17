/**
 * @file undefined.c
 * @author Xu Xiaohong
 * @date 2021/12/06
 * @brief
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

#include "purc.h"

#include "private/debug.h"
#include "private/interpreter.h"

#include "ops.h"

struct ctxt_for_undefined {
    struct pcvdom_node           *curr;
};

static void
ctxt_for_undefined_destroy(struct ctxt_for_undefined *ctxt)
{
    if (ctxt)
        free(ctxt);
}

static void
after_pushed(pcintr_coroutine_t co, struct pcintr_stack_frame *frame)
{
    fprintf(stderr, "==%s[%d]==\n", __FILE__, __LINE__);

    struct ctxt_for_undefined *ctxt;
    ctxt = (struct ctxt_for_undefined*)calloc(1, sizeof(*ctxt));
    if (!ctxt) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        frame->next_step = -1;
        co->state = CO_STATE_TERMINATED;
        return;
    }

    frame->ctxt = ctxt;
    frame->next_step = NEXT_STEP_SELECT_CHILD;
    co->state = CO_STATE_READY;
}

static void
on_popping(pcintr_coroutine_t co, struct pcintr_stack_frame *frame)
{
    fprintf(stderr, "==%s[%d]==\n", __FILE__, __LINE__);
    pcintr_stack_t stack = co->stack;
    struct ctxt_for_undefined *ctxt;
    ctxt = (struct ctxt_for_undefined*)frame->ctxt;
    if (ctxt) {
        ctxt_for_undefined_destroy(ctxt);
        frame->ctxt = NULL;
    }
    pop_stack_frame(stack);
    co->state = CO_STATE_READY;
}

static void
select_child(pcintr_coroutine_t co, struct pcintr_stack_frame *frame)
{
    fprintf(stderr, "==%s[%d]==\n", __FILE__, __LINE__);

    struct ctxt_for_undefined *ctxt;
    ctxt = (struct ctxt_for_undefined*)frame->ctxt;

    if (ctxt->curr == NULL) {
        struct pcvdom_element *element = frame->scope;
        struct pcvdom_node *node = &element->node;
        node = pcvdom_node_first_child(node);
        ctxt->curr = node;
    }
    else {
        ctxt->curr = pcvdom_node_next_sibling(ctxt->curr);
    }

    if (ctxt->curr == NULL) {
        frame->next_step = NEXT_STEP_ON_POPPING;
        co->state = CO_STATE_READY;
        return;
    }

    if (!PCVDOM_NODE_IS_ELEMENT(ctxt->curr)) {
        co->state = CO_STATE_READY;
        return;
    }

    struct pcvdom_element *element = PCVDOM_ELEMENT_FROM_NODE(ctxt->curr);

    pcintr_stack_t stack = co->stack;
    struct pcintr_stack_frame *child_frame;
    child_frame = push_stack_frame(stack);
    if (!child_frame) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return;
    }
    child_frame->ops = pcintr_get_ops_by_element(element);
    child_frame->scope = element;

    ctxt->curr = &element->node;
    frame->next_step = NEXT_STEP_SELECT_CHILD;
    co->state = CO_STATE_READY;
}

static struct pcintr_element_ops
ops = {
    .after_pushed       = after_pushed,
    .on_popping         = on_popping,
    .rerun              = NULL,
    .select_child       = select_child,
};

struct pcintr_element_ops* pcintr_get_undefined_ops(void)
{
    return &ops;
}



