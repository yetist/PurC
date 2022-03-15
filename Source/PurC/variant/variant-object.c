/**
 * @file variant-object.c
 * @author Xu Xiaohong (freemine)
 * @date 2021/07/08
 * @brief The API for variant.
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
 */


#include "config.h"
#include "private/variant.h"
#include "private/errors.h"
#include "purc-errors.h"
#include "variant-internals.h"


#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define OBJ_EXTRA_SIZE(data) (sizeof(*data) + \
        (data->size) * sizeof(struct obj_node))

static inline bool
grow(purc_variant_t obj, purc_variant_t key, purc_variant_t val,
        bool check)
{
    if (!check)
        return true;

    purc_variant_t vals[] = { key, val };

    return pcvariant_on_pre_fired(obj, PCVAR_OPERATION_GROW,
            PCA_TABLESIZE(vals), vals);
}

static inline bool
shrink(purc_variant_t obj, purc_variant_t key, purc_variant_t val,
        bool check)
{
    if (!check)
        return true;

    purc_variant_t vals[] = { key, val };

    return pcvariant_on_pre_fired(obj, PCVAR_OPERATION_SHRINK,
            PCA_TABLESIZE(vals), vals);
}

static inline bool
change(purc_variant_t obj,
        purc_variant_t ko, purc_variant_t vo,
        purc_variant_t kn, purc_variant_t vn,
        bool check)
{
    if (!check)
        return true;

    purc_variant_t vals[] = { ko, vo, kn, vn };

    return pcvariant_on_pre_fired(obj, PCVAR_OPERATION_CHANGE,
            PCA_TABLESIZE(vals), vals);
}

static inline void
grown(purc_variant_t obj, purc_variant_t key, purc_variant_t val,
        bool check)
{
    if (!check)
        return;

    purc_variant_t vals[] = { key, val };

    pcvariant_on_post_fired(obj, PCVAR_OPERATION_GROW,
            PCA_TABLESIZE(vals), vals);
}

static inline void
shrunk(purc_variant_t obj, purc_variant_t key, purc_variant_t val,
        bool check)
{
    if (!check)
        return;

    purc_variant_t vals[] = { key, val };

    pcvariant_on_post_fired(obj, PCVAR_OPERATION_SHRINK,
            PCA_TABLESIZE(vals), vals);
}

static inline void
changed(purc_variant_t obj,
        purc_variant_t ko, purc_variant_t vo,
        purc_variant_t kn, purc_variant_t vn,
        bool check)
{
    if (!check)
        return;

    purc_variant_t vals[] = { ko, vo, kn, vn };

    pcvariant_on_post_fired(obj, PCVAR_OPERATION_CHANGE,
            PCA_TABLESIZE(vals), vals);
}

variant_obj_t
pcvar_obj_get_data(purc_variant_t obj)
{
    variant_obj_t data = (variant_obj_t)obj->sz_ptr[1];
    return data;
}

static purc_variant_t v_object_new_with_capacity(void)
{
    purc_variant_t var = pcvariant_get(PVT(_OBJECT));
    if (!var) {
        pcinst_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return PURC_VARIANT_INVALID;
    }

    var->type          = PVT(_OBJECT);
    var->flags         = PCVARIANT_FLAG_EXTRA_SIZE;

    variant_obj_t data;
    data = (variant_obj_t)calloc(1, sizeof(*data));

    if (!data) {
        pcvariant_put(var);
        pcinst_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return PURC_VARIANT_INVALID;
    }

    data->kvs = RB_ROOT;

    var->sz_ptr[1]     = (uintptr_t)data;
    var->refc          = 1;

    return var;
}

static int
v_object_remove(purc_variant_t obj, purc_variant_t key, bool silently,
        bool check)
{
    variant_obj_t data = pcvar_obj_get_data(obj);
    struct rb_root *root = &data->kvs;
    struct rb_node **pnode = &root->rb_node;
    struct rb_node *parent = NULL;
    struct rb_node *entry = NULL;
    while (*pnode) {
        struct obj_node *node;
        node = container_of(*pnode, struct obj_node, node);
        int ret = purc_variant_compare_ex(key, node->key,
                PCVARIANT_COMPARE_OPT_AUTO);

        parent = *pnode;

        if (ret < 0)
            pnode = &parent->rb_left;
        else if (ret > 0)
            pnode = &parent->rb_right;
        else{
            entry = *pnode;
            break;
        }
    }

    if (!entry) {
        if (silently) {
            return 0;
        }
        pcinst_set_error(PCVARIANT_ERROR_NOT_FOUND);
        return -1;
    }

    struct obj_node *node;
    node = container_of(entry, struct obj_node, node);
    purc_variant_t k = node->key;
    purc_variant_t v = node->val;

    if (!shrink(obj, k, v, check)) {
        return -1;
    }

    struct pcvar_rev_update_edge edge = {
        .parent        = obj,
        .obj_me        = node,
    };
    pcvar_break_edge_to_parent(node->val, &edge);
    pcvar_break_rue_downward(node->val);

    pcutils_rbtree_erase(entry, root);
    --data->size;

    shrunk(obj, k, v, check);

    purc_variant_unref(k);
    purc_variant_unref(v);

    free(node);

    return 0;
}

static int
v_object_set(purc_variant_t obj, purc_variant_t k, purc_variant_t val,
        bool check)
{
    if (!k || !val) {
        pcinst_set_error(PURC_ERROR_INVALID_VALUE);
        return -1;
    }

    if (purc_variant_is_undefined(val)) {
        bool silently = true;
        v_object_remove(obj, k, silently, check);
        return 0;
    }

    if (pcvar_container_belongs_to_set(val)) {
        PC_ASSERT(0);
    }

    variant_obj_t data = pcvar_obj_get_data(obj);
    PC_ASSERT(data);

    struct rb_root *root = &data->kvs;
    struct rb_node **pnode = &root->rb_node;
    struct rb_node *parent = NULL;
    struct rb_node *entry = NULL;
    while (*pnode) {
        struct obj_node *node;
        node = container_of(*pnode, struct obj_node, node);
        int ret = purc_variant_compare_ex(k, node->key,
                PCVARIANT_COMPARE_OPT_AUTO);

        parent = *pnode;

        if (ret < 0)
            pnode = &parent->rb_left;
        else if (ret > 0)
            pnode = &parent->rb_right;
        else{
            entry = *pnode;
            break;
        }
    }

    if (!entry) { //new the entry
        struct obj_node *node = (struct obj_node*)calloc(1, sizeof(*node));
        if (!node) {
            pcinst_set_error(PURC_ERROR_OUT_OF_MEMORY);
            return -1;
        }

        if (!grow(obj, k, val, check)) {
            free(node);
            return -1;
        }

        node->key = k;
        node->val = val;
        purc_variant_ref(k);
        purc_variant_ref(val);
        entry = &node->node;

        pcutils_rbtree_link_node(entry, parent, pnode);
        pcutils_rbtree_insert_color(entry, root);

        ++data->size;

        if (pcvar_container_belongs_to_set(obj)) {
            struct pcvar_rev_update_edge edge = {
                .parent        = obj,
                .obj_me        = node,
            };
            int r = pcvar_build_edge_to_parent(val, &edge);
            if (r == 0) {
                r = pcvar_build_rue_downward(val);
            }
            // TODO: recover
            PC_ASSERT(r == 0);
        }

        grown(obj, k, val, check);
        return 0;
    }

    struct obj_node *node;
    node = container_of(entry, struct obj_node, node);
    if (node->val == val)
        return 0;

    purc_variant_t ko = node->key;
    purc_variant_t vo = node->val;

    if (!change(obj, ko, vo, k, val, check)) {
        return -1;
    }

    if (pcvar_container_belongs_to_set(obj)) {
        struct pcvar_rev_update_edge edge = {
            .parent        = obj,
            .obj_me        = node,
        };
        pcvar_break_edge_to_parent(node->val, &edge);
        pcvar_break_rue_downward(node->val);

        node->key = k;
        node->val = val;
        purc_variant_ref(k);
        purc_variant_ref(val);

        edge.parent = obj;
        edge.obj_me = node;
        int r = pcvar_build_edge_to_parent(node->val, &edge);
        if (r == 0) {
            r = pcvar_build_rue_downward(node->val);
        }
        // FIXME: recoverable?
        PC_ASSERT(r == 0);
    }
    else {
        node->key = k;
        node->val = val;
        purc_variant_ref(k);
        purc_variant_ref(val);
    }

    changed(obj, ko, vo, k, val, check);

    purc_variant_unref(ko);
    purc_variant_unref(vo);

    return 0;
}

static int
v_object_set_kvs_n(purc_variant_t obj, bool check, size_t nr_kv_pairs,
    int is_c, va_list ap)
{
    purc_variant_t k, v;

    size_t i = 0;
    while (i<nr_kv_pairs) {
        if (is_c) {
            const char *k_c = va_arg(ap, const char*);
            k = purc_variant_make_string(k_c, true);
            if (!k)
                break;
        } else {
            k = va_arg(ap, purc_variant_t);
            if (!k || k->type!=PVT(_STRING)) {
                pcinst_set_error(PURC_ERROR_INVALID_VALUE);
                break;
            }
        }
        v = va_arg(ap, purc_variant_t);

        int r = v_object_set(obj, k, v, check);
        if (is_c)
            purc_variant_unref(k);

        if (r)
            break;
        ++i;
    }
    return i<nr_kv_pairs ? -1 : 0;
}

static purc_variant_t
pv_make_object_by_static_ckey_n (bool check, size_t nr_kv_pairs,
    const char* key0, purc_variant_t value0, va_list ap)
{
    PCVARIANT_CHECK_FAIL_RET((nr_kv_pairs==0 && key0==NULL && value0==NULL) ||
                         (nr_kv_pairs>0 && key0 && value0),
        PURC_VARIANT_INVALID);

    purc_variant_t obj = v_object_new_with_capacity();
    if (!obj)
        return PURC_VARIANT_INVALID;

    variant_obj_t data = pcvar_obj_get_data(obj);
    PC_ASSERT(data);

    do {
        int r;
        if (nr_kv_pairs > 0) {
            purc_variant_t k = purc_variant_make_string(key0, true);
            purc_variant_t v = value0;
            r = v_object_set(obj, k, v, check);
            purc_variant_unref(k);
            if (r)
                break;
        }

        if (nr_kv_pairs > 1) {
            r = v_object_set_kvs_n(obj, check, nr_kv_pairs-1, 1, ap);
            if (r)
                break;
        }

        size_t extra = OBJ_EXTRA_SIZE(data);
        pcvariant_stat_set_extra_size(obj, extra);

        return obj;
    } while (0);

    // cleanup
    purc_variant_unref(obj);

    return PURC_VARIANT_INVALID;
}

purc_variant_t
purc_variant_make_object_by_static_ckey (size_t nr_kv_pairs,
    const char* key0, purc_variant_t value0, ...)
{
    bool check = true;
    purc_variant_t v;
    va_list ap;
    va_start(ap, value0);
    v = pv_make_object_by_static_ckey_n(check, nr_kv_pairs, key0, value0, ap);
    va_end(ap);

    return v;
}

static purc_variant_t
pv_make_object_n(bool check, size_t nr_kv_pairs,
    purc_variant_t key0, purc_variant_t value0, va_list ap)
{
    PCVARIANT_CHECK_FAIL_RET((nr_kv_pairs==0 && key0==NULL && value0==NULL) ||
                         (nr_kv_pairs>0 && key0 && value0),
        PURC_VARIANT_INVALID);

    purc_variant_t obj = v_object_new_with_capacity();
    if (!obj)
        return PURC_VARIANT_INVALID;

    variant_obj_t data = pcvar_obj_get_data(obj);
    PC_ASSERT(data);

    do {
        if (nr_kv_pairs > 0) {
            purc_variant_t v = value0;
            if (v_object_set(obj, key0, v, check))
                break;
        }

        if (nr_kv_pairs > 1) {
            int r = v_object_set_kvs_n(obj, check, nr_kv_pairs-1, 0, ap);
            if (r)
                break;
        }

        variant_obj_t data = pcvar_obj_get_data(obj);
        size_t extra = OBJ_EXTRA_SIZE(data);
        pcvariant_stat_set_extra_size(obj, extra);

        return obj;
    } while (0);

    // cleanup
    purc_variant_unref(obj);

    return PURC_VARIANT_INVALID;
}

purc_variant_t
purc_variant_make_object (size_t nr_kv_pairs,
    purc_variant_t key0, purc_variant_t value0, ...)
{
    bool check = true;
    purc_variant_t v;
    va_list ap;
    va_start(ap, value0);
    v = pv_make_object_n(check, nr_kv_pairs, key0, value0, ap);
    va_end(ap);

    return v;
}

void pcvariant_object_release (purc_variant_t value)
{
    variant_obj_t data = pcvar_obj_get_data(value);

    struct rb_root *root = &data->kvs;

    struct rb_node *p = pcutils_rbtree_first(root);
    while (p) {
        struct rb_node *next = pcutils_rbtree_next(p);
        struct obj_node *node;
        node = container_of(p, struct obj_node, node);

        struct pcvar_rev_update_edge edge = {
            .parent        = value,
            .obj_me        = node,
        };
        pcvar_break_edge_to_parent(node->val, &edge);
        pcvar_break_rue_downward(node->val);

        pcutils_rbtree_erase(p, root);
        PURC_VARIANT_SAFE_CLEAR(node->key);
        PURC_VARIANT_SAFE_CLEAR(node->val);
        free(node);
        p = next;
    }
    free(data);

    value->sz_ptr[1] = (uintptr_t)NULL; // say no to double free

    pcvariant_stat_set_extra_size(value, 0);
}

/* VWNOTE: unnecessary
int pcvariant_object_compare (purc_variant_t lv, purc_variant_t rv)
{
    // only called via purc_variant_compare
    struct pchash_table *lht = pcvar_obj_get_data(lv);
    struct pchash_table *rht = pcvar_obj_get_data(rv);

    struct pchash_entry *lcurr = lht->head;
    struct pchash_entry *rcurr = rht->head;

    for (; lcurr && rcurr; lcurr=lcurr->next, rcurr=rcurr->next) {
        int r = pcvariant_object_compare(
                    (purc_variant_t)pchash_entry_v(lcurr),
                    (purc_variant_t)pchash_entry_v(rcurr));
        if (r)
            return r;
    }

    return lcurr ? 1 : -1;
}
*/

purc_variant_t purc_variant_object_get(purc_variant_t obj, purc_variant_t key,
        bool silently)
{
    PCVARIANT_CHECK_FAIL_RET((obj && obj->type==PVT(_OBJECT) &&
        obj->sz_ptr[1] && key),
        PURC_VARIANT_INVALID);

    variant_obj_t data = pcvar_obj_get_data(obj);
    struct rb_root *root = &data->kvs;

    struct rb_node **pnode = &root->rb_node;
    struct rb_node *parent = NULL;
    struct rb_node *entry = NULL;
    while (*pnode) {
        struct obj_node *node;
        node = container_of(*pnode, struct obj_node, node);
        int ret = purc_variant_compare_ex(key, node->key,
                PCVARIANT_COMPARE_OPT_AUTO);

        parent = *pnode;

        if (ret < 0)
            pnode = &parent->rb_left;
        else if (ret > 0)
            pnode = &parent->rb_right;
        else{
            entry = *pnode;
            break;
        }
    }

    if (!entry) {
        if (!silently) {
            pcinst_set_error(PCVARIANT_ERROR_NOT_FOUND);
        }
        return PURC_VARIANT_INVALID;
    }

    struct obj_node *node;
    node = container_of(entry, struct obj_node, node);
    return node->val;
}

bool purc_variant_object_set (purc_variant_t obj,
    purc_variant_t key, purc_variant_t value)
{
    PCVARIANT_CHECK_FAIL_RET(obj && obj->type==PVT(_OBJECT) &&
        obj->sz_ptr[1] && key && value,
        false);

    bool check = true;
    int r = v_object_set(obj, key, value, check);

    return r ? false : true;
}

bool purc_variant_object_remove(purc_variant_t obj, purc_variant_t key,
        bool silently)
{
    PCVARIANT_CHECK_FAIL_RET(obj && obj->type==PVT(_OBJECT) &&
        obj->sz_ptr[1] && key,
        false);

    bool check = true;
    if (v_object_remove(obj, key, silently, check))
        return false;

    return true;
}

bool purc_variant_object_size (purc_variant_t obj, size_t *sz)
{
    PC_ASSERT(obj && sz);

    PCVARIANT_CHECK_FAIL_RET(obj->type == PVT(_OBJECT) && obj->sz_ptr[1],
        false);

    variant_obj_t data = pcvar_obj_get_data(obj);
    *sz = (size_t)data->size;

    return true;
}

struct purc_variant_object_iterator {
    struct obj_iterator it;
};

struct purc_variant_object_iterator*
purc_variant_object_make_iterator_begin (purc_variant_t object)
{
    PCVARIANT_CHECK_FAIL_RET((object && object->type==PVT(_OBJECT) &&
        object->sz_ptr[1]),
        NULL);

    variant_obj_t data = pcvar_obj_get_data(object);
    if (data->size==0) {
        pcinst_set_error(PCVARIANT_ERROR_NOT_FOUND);
        return NULL;
    }

    struct purc_variant_object_iterator *it;
    it = (struct purc_variant_object_iterator*)malloc(sizeof(*it));
    if (!it) {
        pcinst_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return NULL;
    }

    it->it = pcvar_obj_it_first(object);

    return it;
}

struct purc_variant_object_iterator*
purc_variant_object_make_iterator_end (purc_variant_t object) {
    PCVARIANT_CHECK_FAIL_RET((object && object->type==PVT(_OBJECT) &&
        object->sz_ptr[1]),
        NULL);

    variant_obj_t data = pcvar_obj_get_data(object);
    if (data->size==0) {
        pcinst_set_error(PCVARIANT_ERROR_NOT_FOUND);
        return NULL;
    }

    struct purc_variant_object_iterator *it;
    it = (struct purc_variant_object_iterator*)malloc(sizeof(*it));
    if (!it) {
        pcinst_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return NULL;
    }

    it->it = pcvar_obj_it_last(object);

    return it;
}

void
purc_variant_object_release_iterator (struct purc_variant_object_iterator* it)
{
    if (!it)
        return;

    it->it.obj  = PURC_VARIANT_INVALID;
    it->it.curr = NULL;
    it->it.next = NULL;
    it->it.prev = NULL;

    free(it);
}

bool
purc_variant_object_iterator_next (struct purc_variant_object_iterator* it)
{
    PC_ASSERT(it);

    pcvar_obj_it_next(&it->it);

    if (it->it.curr)
        return true;

    return false;
}

bool
purc_variant_object_iterator_prev (struct purc_variant_object_iterator* it)
{
    PC_ASSERT(it);

    pcvar_obj_it_prev(&it->it);

    if (it->it.curr)
        return true;

    return false;
}

purc_variant_t
purc_variant_object_iterator_get_key (struct purc_variant_object_iterator* it)
{
    PC_ASSERT(it);

    if (it->it.curr == NULL)
        return PURC_VARIANT_INVALID;

    return it->it.curr->key;
}

purc_variant_t
purc_variant_object_iterator_get_value(struct purc_variant_object_iterator* it)
{
    PC_ASSERT(it);

    if (it->it.curr == NULL)
        return PURC_VARIANT_INVALID;

    return it->it.curr->val;
}

purc_variant_t
pcvariant_object_clone(purc_variant_t obj, bool recursively)
{
    purc_variant_t var;
    var = purc_variant_make_object(0,
            PURC_VARIANT_INVALID, PURC_VARIANT_INVALID);
    if (var == PURC_VARIANT_INVALID)
        return PURC_VARIANT_INVALID;

    purc_variant_t k,v;
    foreach_key_value_in_variant_object(obj, k, v) {
        purc_variant_t val;
        if (recursively) {
            val = pcvariant_container_clone(v, recursively);
        }
        else {
            val = purc_variant_ref(v);
        }
        if (val == PURC_VARIANT_INVALID) {
            purc_variant_unref(var);
            return PURC_VARIANT_INVALID;
        }
        bool ok;
        ok = purc_variant_object_set(var, k, val);
        purc_variant_unref(val);
        if (!ok) {
            purc_variant_unref(var);
            return PURC_VARIANT_INVALID;
        }
    } end_foreach;

    PC_ASSERT(var != obj);
    return var;
}

void
pcvar_object_break_rue_downward(purc_variant_t obj)
{
    PC_ASSERT(purc_variant_is_object(obj));
    variant_obj_t data = (variant_obj_t)obj->sz_ptr[1];
    if (!data)
        return;

    struct rb_root *root = &data->kvs;
    struct rb_node *p = pcutils_rbtree_first(root);
    for (; p; p = pcutils_rbtree_next(p)) {
        struct obj_node *node;
        node = container_of(p, struct obj_node, node);
        struct pcvar_rev_update_edge edge = {
            .parent         = obj,
            .obj_me         = node,
        };
        pcvar_break_edge_to_parent(node->val, &edge);
        pcvar_break_rue_downward(node->val);
    }
}

void
pcvar_object_break_edge_to_parent(purc_variant_t obj,
        struct pcvar_rev_update_edge *edge)
{
    PC_ASSERT(purc_variant_is_object(obj));
    variant_obj_t data = (variant_obj_t)obj->sz_ptr[1];
    if (!data)
        return;

    pcvar_break_edge(obj, &data->rev_update_chain, edge);
}

int
pcvar_object_build_rue_downward(purc_variant_t obj)
{
    PC_ASSERT(purc_variant_is_object(obj));
    variant_obj_t data = (variant_obj_t)obj->sz_ptr[1];
    if (!data)
        return 0;

    struct rb_root *root = &data->kvs;
    struct rb_node *p = pcutils_rbtree_first(root);
    for (; p; p = pcutils_rbtree_next(p)) {
        struct obj_node *node;
        node = container_of(p, struct obj_node, node);
        struct pcvar_rev_update_edge edge = {
            .parent         = obj,
            .obj_me         = node,
        };
        int r = pcvar_build_edge_to_parent(node->val, &edge);
        if (r)
            return -1;
        r = pcvar_build_rue_downward(node->val);
        if (r)
            return -1;
    }

    return 0;
}

int
pcvar_object_build_edge_to_parent(purc_variant_t obj,
        struct pcvar_rev_update_edge *edge)
{
    PC_ASSERT(purc_variant_is_object(obj));
    variant_obj_t data = (variant_obj_t)obj->sz_ptr[1];
    if (!data)
        return 0;

    return pcvar_build_edge(obj, &data->rev_update_chain, edge);
}

static void
it_refresh(struct obj_iterator *it, struct rb_node *curr)
{
    struct rb_node *next  = NULL;
    struct rb_node *prev  = NULL;
    if (curr) {
        next  = pcutils_rbtree_next(curr);
        prev  = pcutils_rbtree_prev(curr);
    }

    if (curr) {
        it->curr = container_of(curr, struct obj_node, node);
    }
    else {
        it->curr = NULL;
    }

    if (next) {
        it->next = container_of(next, struct obj_node, node);
    }
    else {
        it->next = NULL;
    }

    if (prev) {
        it->prev = container_of(prev, struct obj_node, node);
    }
    else {
        it->prev = NULL;
    }
}

struct obj_iterator
pcvar_obj_it_first(purc_variant_t obj)
{
    struct obj_iterator it = {
        .obj         = obj,
    };
    if (obj == PURC_VARIANT_INVALID)
        return it;

    variant_obj_t data = pcvar_obj_get_data(obj);
    if (data->size==0)
        return it;

    struct rb_root *root = &data->kvs;

    struct rb_node *first = pcutils_rbtree_first(root);
    it_refresh(&it, first);

    return it;
}

struct obj_iterator
pcvar_obj_it_last(purc_variant_t obj)
{
    struct obj_iterator it = {
        .obj         = obj,
    };
    if (obj == PURC_VARIANT_INVALID)
        return it;

    variant_obj_t data = pcvar_obj_get_data(obj);
    if (data->size==0)
        return it;

    struct rb_root *root = &data->kvs;

    struct rb_node *last = pcutils_rbtree_last(root);
    it_refresh(&it, last);

    return it;
}

void
pcvar_obj_it_next(struct obj_iterator *it)
{
    if (it->curr == NULL)
        return;

    if (it->next) {
        struct rb_node *next = &it->next->node;
        it_refresh(it, next);
    }
    else {
        it->curr = NULL;
        it->next = NULL;
        it->prev = NULL;
    }
}

void
pcvar_obj_it_prev(struct obj_iterator *it)
{
    if (it->curr == NULL)
        return;

    if (it->prev) {
        struct rb_node *prev = &it->prev->node;
        it_refresh(it, prev);
    }
    else {
        it->curr = NULL;
        it->next = NULL;
        it->prev = NULL;
    }
}

