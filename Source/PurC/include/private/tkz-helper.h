/*
 * @file tkz-helper.h
 * @author Xue Shuming
 * @date 2022/04/22
 * @brief The helper function for ejson/hvml tokenizer.
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

#ifndef PURC_TKZ_HELPER_H
#define PURC_TKZ_HELPER_H

#include "config.h"

#include "private/sbst.h"
#define PCHTML_HTML_TOKENIZER_RES_ENTITIES_SBST
#include "html/tokenizer/res.h"

#include "private/instance.h"
#include "private/errors.h"
#include "private/debug.h"
#include "private/utils.h"
#include "private/list.h"

#define TKZ_END_OF_FILE          0
#define TKZ_INVALID_CHARACTER    0xFFFFFFFF

struct tkz_reader;
struct tkz_uc {
    struct list_head list;
    uint32_t character;
    int line;
    int column;
    int position;
};

struct tkz_buffer {
    uint8_t *base;
    uint8_t *here;
    uint8_t *stop;
    size_t nr_chars;
};

struct tkz_sbst;

PCA_EXTERN_C_BEGIN

PCA_INLINE bool
is_eof(uint32_t c)
{
    return c == TKZ_END_OF_FILE;
}

PCA_INLINE bool
is_whitespace(uint32_t c)
{
    switch (c) {
    case ' ':
    case 0x0A:
    case 0x09:
    case 0x0C:
        return true;
    }
    return false;
}

PCA_INLINE uint32_t
to_ascii_lower_unchecked(uint32_t c)
{
    return c | 0x20;
}

PCA_INLINE bool
is_ascii(uint32_t c)
{
    return !(c & ~0x7F);
}

PCA_INLINE bool
is_ascii_lower(uint32_t c)
{
    return c >= 'a' && c <= 'z';
}

PCA_INLINE bool
is_ascii_upper(uint32_t c)
{
    return c >= 'A' && c <= 'Z';
}

PCA_INLINE bool
is_ascii_space(uint32_t c)
{
    return c <= ' ' && (c == ' ' || (c <= 0xD && c >= 0x9));
}

PCA_INLINE bool
is_ascii_digit(uint32_t c)
{
    return c >= '0' && c <= '9';
}

PCA_INLINE bool
is_ascii_binary_digit(uint32_t c)
{
    return c == '0' || c == '1';
}

PCA_INLINE bool
is_ascii_hex_digit(uint32_t c)
{
    return is_ascii_digit(c) || (
            to_ascii_lower_unchecked(c) >= 'a' &&
            to_ascii_lower_unchecked(c) <= 'f'
            );
}

PCA_INLINE bool
is_ascii_upper_hex_digit(uint32_t c)
{
    return is_ascii_digit(c) || (c >= 'A' && c <= 'F');
}

PCA_INLINE bool
is_ascii_lower_hex_digit(uint32_t c)
{
    return is_ascii_digit(c) || (c >= 'a' && c <= 'f');
}

PCA_INLINE bool
is_ascii_octal_digit(uint32_t c)
{
    return c >= '0' && c <= '7';
}

PCA_INLINE bool
is_ascii_alpha(uint32_t c)
{
    return is_ascii_lower(to_ascii_lower_unchecked(c));
}

PCA_INLINE bool
is_ascii_alpha_numeric(uint32_t c)
{
    return is_ascii_digit(c) || is_ascii_alpha(c);
}

PCA_INLINE bool
is_separator(uint32_t c)
{
    switch (c) {
    case '{':
    case '}':
    case '[':
    case ']':
    case '<':
    case '>':
    case '(':
    case ')':
    case ',':
    case ':':
        return true;
    }
    return false;
}

PCA_INLINE bool
is_delimiter(uint32_t c)
{
    switch (c) {
    case TKZ_END_OF_FILE:
    case ' ':
    case 0x0A:
    case 0x09:
    case 0x0C:
    case '{':
    case '}':
    case '[':
    case ']':
    case '(':
    case ')':
    case '<':
    case '>':
    case '$':
    case ':':
    case ';':
    case '&':
    case '|':
        return true;
    }
    return false;
}

PCA_INLINE bool
is_attribute_value_operator(uint32_t c)
{
    switch (c) {
    case '+':
    case '-':
    case '*':
    case '/':
    case '%':
    case '~':
    case '^':
    case '$':
        return true;
    }
    return false;
}

PCA_INLINE bool
is_context_variable(uint32_t c)
{
    switch (c) {
    case '?':
    case '@':
    case '!':
    case '^':
    case ':':
    case '=':
    case '%':
    case '<':
        return true;
    }
    return false;
}

// tokenizer reader
struct tkz_reader *tkz_reader_new(void);

void tkz_reader_set_rwstream(struct tkz_reader *reader, purc_rwstream_t rws);

struct tkz_uc *tkz_reader_next_char(struct tkz_reader *reader);

bool tkz_reader_reconsume_last_char(struct tkz_reader *reader);

void tkz_reader_destroy(struct tkz_reader *reader);


// tokenizer buffer
struct tkz_buffer *tkz_buffer_new(void);

PCA_INLINE
bool
tkz_buffer_is_empty(struct tkz_buffer *buffer)
{
    return buffer->here == buffer->base;
}

PCA_INLINE size_t
tkz_buffer_get_size_in_bytes(struct tkz_buffer *buffer)
{
    return buffer->here - buffer->base;
}

PCA_INLINE size_t
tkz_buffer_get_size_in_chars(struct tkz_buffer *buffer)
{
    return buffer->nr_chars;
}

PCA_INLINE const char *
tkz_buffer_get_bytes(struct tkz_buffer *buffer)
{
    return(const char*)buffer->base;
}

void
tkz_buffer_append_bytes(struct tkz_buffer *buffer, const char *bytes,
        size_t nr_bytes);

void
tkz_buffer_append(struct tkz_buffer *buffer, uint32_t uc);

void
tkz_buffer_append_chars(struct tkz_buffer *buffer, const uint32_t *ucs,
        size_t nr_ucs);

PCA_INLINE void
tkz_buffer_append_another(struct tkz_buffer *buffer,
        struct tkz_buffer *another)
{
    tkz_buffer_append_bytes(buffer,
        tkz_buffer_get_bytes(another),
        tkz_buffer_get_size_in_bytes(another));
}

/*
 * delete characters from head
 */
void
tkz_buffer_delete_head_chars(struct tkz_buffer *buffer, size_t sz);

/*
 * delete characters from tail
 */
void
tkz_buffer_delete_tail_chars(struct tkz_buffer *buffer, size_t sz);

bool
tkz_buffer_start_with(struct tkz_buffer *buffer, const char *bytes,
        size_t nr_bytes);

bool
tkz_buffer_end_with(struct tkz_buffer *buffer, const char *bytes,
        size_t nr_bytes);

bool
tkz_buffer_equal_to(struct tkz_buffer *buffer, const char *bytes,
        size_t nr_bytes);

uint32_t
tkz_buffer_get_last_char(struct tkz_buffer *buffer);

bool
tkz_buffer_is_int(struct tkz_buffer *buffer);

bool
tkz_buffer_is_number(struct tkz_buffer *buffer);

bool
tkz_buffer_is_whitespace(struct tkz_buffer *buffer);

void
tkz_buffer_reset(struct tkz_buffer *buffer);

void
tkz_buffer_destroy(struct tkz_buffer *buffer);

// sbst
struct tkz_sbst *tkz_sbst_new_char_ref(void);
struct tkz_sbst *tkz_sbst_new_markup_declaration_open_state(void);
struct tkz_sbst *tkz_sbst_new_after_doctype_name_state(void);
struct tkz_sbst *tkz_sbst_new_ejson_keywords(void);

void tkz_sbst_destroy(struct tkz_sbst *sbst);

bool tkz_sbst_advance_ex(struct tkz_sbst *sbst, uint32_t uc,
        bool case_insensitive);

/*
 * case sensitive
 */
PCA_INLINE bool
tkz_sbst_advance(struct tkz_sbst *sbst, uint32_t uc)
{
    return tkz_sbst_advance_ex(sbst, uc, false);
}

const char*
tkz_sbst_get_match(struct tkz_sbst *sbst);

/*
 * return arraylist of unicode character (uint32_t)
 */
struct pcutils_arrlist*
tkz_sbst_get_buffered_ucs(struct tkz_sbst *sbst);

int
tkz_set_error_info(struct tkz_uc *uc, int error);

PCA_EXTERN_C_END

#endif /* PURC_TKZ_HELPER_H */
