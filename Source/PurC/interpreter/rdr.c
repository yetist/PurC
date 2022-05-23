/*
 * @file rdr.c
 * @author XueShuming
 * @date 2022/03/09
 * @brief The impl of the interaction between interpreter and renderer.
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

#include "purc.h"
#include "config.h"
#include "internal.h"

#include "private/errors.h"
#include "private/instance.h"
#include "private/utils.h"
#include "private/variant.h"
#include "private/pcrdr.h"

#include <string.h>

#define ID_KEY                  "id"
#define TITLE_KEY               "title"
#define STYLE_KEY               "style"
#define LEVEL_KEY               "level"
#define CLASS_KEY               "class"

#define BUFF_MIN                1024
#define BUFF_MAX                1024 * 4
#define LEN_BUFF_LONGLONGINT    128

static bool
object_set(purc_variant_t object, const char* key, const char* value)
{
    purc_variant_t k = purc_variant_make_string_static(key, false);
    if (k == PURC_VARIANT_INVALID) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return false;
    }

    purc_variant_t v = purc_variant_make_string_static(value, false);
    if (k == PURC_VARIANT_INVALID) {
        purc_variant_unref(k);
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        return false;
    }

    purc_variant_object_set(object, k, v);
    purc_variant_unref(k);
    purc_variant_unref(v);
    return true;
}

pcrdr_msg *pcintr_rdr_send_request_and_wait_response(struct pcrdr_conn *conn,
        pcrdr_msg_target target, uint64_t target_value, const char *operation,
        pcrdr_msg_element_type element_type, const char *element,
        const char *property, pcrdr_msg_data_type data_type,
        purc_variant_t data)
{
    pcrdr_msg *response_msg = NULL;
    pcrdr_msg* msg = pcrdr_make_request_message(
            target,                             /* target */
            target_value,                       /* target_value */
            operation,                          /* operation */
            NULL,                               /* request_id */
            element_type,                       /* element_type */
            element,                            /* element */
            property,                           /* property */
            PCRDR_MSG_DATA_TYPE_VOID,           /* data_type */
            NULL,                               /* data */
            0                                   /* data_len */
            );
    if (msg == NULL) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        goto failed;
    }

    msg->dataType = data_type;
    msg->data = data;

    if (pcrdr_send_request_and_wait_response(conn,
            msg, PCRDR_TIME_DEF_EXPECTED, &response_msg) < 0) {
        goto failed;
    }
    pcrdr_release_message(msg);
    msg = NULL;

    return response_msg;

failed:
    if (msg) {
        pcrdr_release_message(msg);
    }

    return NULL;
}

uintptr_t pcintr_rdr_create_workspace(struct pcrdr_conn *conn,
        uintptr_t session, const char *id, const char *title,
        const char* classes, const char *style)
{
    uintptr_t workspace = 0;
    pcrdr_msg *response_msg = NULL;

    const char *operation = PCRDR_OPERATION_CREATEWORKSPACE;
    pcrdr_msg_target target = PCRDR_MSG_TARGET_SESSION;
    uint64_t target_value = session;
    pcrdr_msg_element_type element_type = PCRDR_MSG_ELEMENT_TYPE_VOID;
    pcrdr_msg_data_type data_type = PCRDR_MSG_DATA_TYPE_EJSON;
    purc_variant_t data = PURC_VARIANT_INVALID;

    data = purc_variant_make_object(0, NULL, NULL);
    if (data == PURC_VARIANT_INVALID) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        goto failed;
    }

    if (!object_set(data, ID_KEY, id)) {
        goto failed;
    }

    if (title && !object_set(data, TITLE_KEY, title)) {
        goto failed;
    }

    if (classes && !object_set(data, CLASS_KEY, classes)) {
        goto failed;
    }

    if (style && !object_set(data, STYLE_KEY, style)) {
        goto failed;
    }

    response_msg = pcintr_rdr_send_request_and_wait_response(conn, target,
            target_value, operation, element_type, NULL, NULL, data_type,
            data);

    if (response_msg == NULL) {
        goto failed;
    }

    int ret_code = response_msg->retCode;
    if (ret_code == PCRDR_SC_OK) {
        workspace = response_msg->resultValue;
    }

    pcrdr_release_message(response_msg);

    if (ret_code != PCRDR_SC_OK) {
        purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
        goto failed;
    }

    return workspace;

failed:
    if (data != PURC_VARIANT_INVALID) {
        purc_variant_unref(data);
    }

    return 0;
}

bool pcintr_rdr_destroy_workspace(struct pcrdr_conn *conn,
        uintptr_t session, uintptr_t workspace)
{
    pcrdr_msg *response_msg = NULL;

    const char *operation = PCRDR_OPERATION_DESTROYWORKSPACE;
    pcrdr_msg_target target = PCRDR_MSG_TARGET_SESSION;
    uint64_t target_value = session;
    pcrdr_msg_element_type element_type = PCRDR_MSG_ELEMENT_TYPE_HANDLE;
    pcrdr_msg_data_type data_type = PCRDR_MSG_DATA_TYPE_VOID;
    purc_variant_t data = PURC_VARIANT_INVALID;

    char element[LEN_BUFF_LONGLONGINT];
    int n = snprintf(element, sizeof(element),
            "%llx", (unsigned long long int)workspace);
    if (n < 0) {
        purc_set_error(PURC_ERROR_BAD_STDC_CALL);
        goto failed;
    }
    else if ((size_t)n >= sizeof (element)) {
        PC_DEBUG ("Too small elementer to serialize message.\n");
        purc_set_error(PURC_ERROR_TOO_SMALL_BUFF);
        goto failed;
    }

    response_msg = pcintr_rdr_send_request_and_wait_response(conn, target,
            target_value, operation, element_type, element, NULL,
            data_type, data);

    if (response_msg == NULL) {
        goto failed;
    }

    int ret_code = response_msg->retCode;
    if (ret_code != PCRDR_SC_OK) {
        purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
        goto failed;
    }

    pcrdr_release_message(response_msg);
    return true;

failed:
    if (data != PURC_VARIANT_INVALID) {
        purc_variant_unref(data);
    }

    if (response_msg) {
        pcrdr_release_message(response_msg);
    }

    return false;
}

bool pcintr_rdr_update_workspace(struct pcrdr_conn *conn,
        uintptr_t session, uintptr_t workspace,
        const char *property, const char *value)
{
    pcrdr_msg *response_msg = NULL;

    const char *operation = PCRDR_OPERATION_UPDATEWORKSPACE;
    pcrdr_msg_target target = PCRDR_MSG_TARGET_SESSION;
    uint64_t target_value = session;
    pcrdr_msg_element_type element_type = PCRDR_MSG_ELEMENT_TYPE_HANDLE;
    pcrdr_msg_data_type data_type = PCRDR_MSG_DATA_TYPE_TEXT;
    purc_variant_t data = PURC_VARIANT_INVALID;

    data = purc_variant_make_string(value, false);
    if (data == PURC_VARIANT_INVALID) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        goto failed;
    }

    char element[LEN_BUFF_LONGLONGINT];
    int n = snprintf(element, sizeof(element),
            "%llx", (unsigned long long int)workspace);
    if (n < 0) {
        purc_set_error(PURC_ERROR_BAD_STDC_CALL);
        goto failed;
    }
    else if ((size_t)n >= sizeof (element)) {
        PC_DEBUG ("Too small elementer to serialize message.\n");
        purc_set_error(PURC_ERROR_TOO_SMALL_BUFF);
        goto failed;
    }

    response_msg = pcintr_rdr_send_request_and_wait_response(conn, target,
            target_value, operation, element_type, element, property,
            data_type, data);

    if (response_msg == NULL) {
        goto failed;
    }

    int ret_code = response_msg->retCode;
    if (ret_code != PCRDR_SC_OK) {
        purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
        goto failed;
    }

    pcrdr_release_message(response_msg);
    return true;

failed:
    if (data != PURC_VARIANT_INVALID) {
        purc_variant_unref(data);
    }

    if (response_msg) {
        pcrdr_release_message(response_msg);
    }

    return false;
}

uintptr_t pcintr_rdr_create_plain_window(struct pcrdr_conn *conn,
        uintptr_t session, uintptr_t workspace, const char *id,
        const char *title, const char* classes, const char *style,
        const char* level)
{
    uintptr_t plain_window = 0;
    pcrdr_msg *response_msg = NULL;

    const char *operation = PCRDR_OPERATION_CREATEPLAINWINDOW;
    pcrdr_msg_target target;
    uint64_t target_value;
    pcrdr_msg_element_type element_type = PCRDR_MSG_ELEMENT_TYPE_VOID;
    pcrdr_msg_data_type data_type = PCRDR_MSG_DATA_TYPE_EJSON;
    purc_variant_t data = PURC_VARIANT_INVALID;

    if (workspace) {
        target = PCRDR_MSG_TARGET_WORKSPACE;
        target_value = workspace;
    }
    else {
        target = PCRDR_MSG_TARGET_SESSION;
        target_value = session;
    }

    data = purc_variant_make_object(0, NULL, NULL);
    if (data == PURC_VARIANT_INVALID) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        goto failed;
    }

    if (!object_set(data, ID_KEY, id)) {
        goto failed;
    }

    if (title && !object_set(data, TITLE_KEY, title)) {
        goto failed;
    }

    if (classes && !object_set(data, CLASS_KEY, classes)) {
        goto failed;
    }

    if (style && !object_set(data, STYLE_KEY, style)) {
        goto failed;
    }

    if (level && !object_set(data, LEVEL_KEY, level)) {
        goto failed;
    }

    response_msg = pcintr_rdr_send_request_and_wait_response(conn, target,
            target_value, operation, element_type, NULL, NULL, data_type,
            data);

    if (response_msg == NULL) {
        goto failed;
    }

    int ret_code = response_msg->retCode;
    if (ret_code == PCRDR_SC_OK) {
        plain_window = response_msg->resultValue;
    }

    pcrdr_release_message(response_msg);

    if (ret_code != PCRDR_SC_OK) {
        purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
        goto failed;
    }

    return plain_window;

failed:
    if (data != PURC_VARIANT_INVALID) {
        purc_variant_unref(data);
    }

    return 0;
}

bool pcintr_rdr_destroy_plain_window(struct pcrdr_conn *conn,
        uintptr_t session, uintptr_t workspace, uintptr_t plain_window)
{
    pcrdr_msg *response_msg = NULL;

    const char *operation = PCRDR_OPERATION_DESTROYPLAINWINDOW;
    pcrdr_msg_target target;
    uint64_t target_value;
    pcrdr_msg_element_type element_type = PCRDR_MSG_ELEMENT_TYPE_HANDLE;
    pcrdr_msg_data_type data_type = PCRDR_MSG_DATA_TYPE_VOID;
    purc_variant_t data = PURC_VARIANT_INVALID;

    if (workspace) {
        target = PCRDR_MSG_TARGET_WORKSPACE;
        target_value = workspace;
    }
    else {
        target = PCRDR_MSG_TARGET_SESSION;
        target_value = session;
    }

    char element[LEN_BUFF_LONGLONGINT];
    int n = snprintf(element, sizeof(element),
            "%llx", (unsigned long long int)plain_window);
    if (n < 0) {
        purc_set_error(PURC_ERROR_BAD_STDC_CALL);
        goto failed;
    }
    else if ((size_t)n >= sizeof (element)) {
        PC_DEBUG ("Too small elementer to serialize message.\n");
        purc_set_error(PURC_ERROR_TOO_SMALL_BUFF);
        goto failed;
    }

    response_msg = pcintr_rdr_send_request_and_wait_response(conn, target,
            target_value, operation, element_type, element, NULL,
            data_type, data);

    if (response_msg == NULL) {
        goto failed;
    }

    int ret_code = response_msg->retCode;
    if (ret_code != PCRDR_SC_OK) {
        purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
        goto failed;
    }

    pcrdr_release_message(response_msg);
    return true;

failed:
    if (data != PURC_VARIANT_INVALID) {
        purc_variant_unref(data);
    }

    if (response_msg) {
        pcrdr_release_message(response_msg);
    }

    return false;
}

// property: title, class, style
bool pcintr_rdr_update_plain_window(struct pcrdr_conn *conn,
        uintptr_t session, uintptr_t workspace, uintptr_t plain_window,
        const char *property, const char *value)
{
    pcrdr_msg *response_msg = NULL;

    const char *operation = PCRDR_OPERATION_UPDATEPLAINWINDOW;
    pcrdr_msg_target target;
    uint64_t target_value;
    pcrdr_msg_element_type element_type = PCRDR_MSG_ELEMENT_TYPE_HANDLE;
    pcrdr_msg_data_type data_type = PCRDR_MSG_DATA_TYPE_TEXT;
    purc_variant_t data = PURC_VARIANT_INVALID;

    if (workspace) {
        target = PCRDR_MSG_TARGET_WORKSPACE;
        target_value = workspace;
    }
    else {
        target = PCRDR_MSG_TARGET_SESSION;
        target_value = session;
    }

    data = purc_variant_make_string(value, false);
    if (data == PURC_VARIANT_INVALID) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        goto failed;
    }

    char element[LEN_BUFF_LONGLONGINT];
    int n = snprintf(element, sizeof(element),
            "%llx", (unsigned long long int)plain_window);
    if (n < 0) {
        purc_set_error(PURC_ERROR_BAD_STDC_CALL);
        goto failed;
    }
    else if ((size_t)n >= sizeof (element)) {
        PC_DEBUG ("Too small elementer to serialize message.\n");
        purc_set_error(PURC_ERROR_TOO_SMALL_BUFF);
        goto failed;
    }

    response_msg = pcintr_rdr_send_request_and_wait_response(conn, target,
            target_value, operation, element_type, element, property,
            data_type, data);

    if (response_msg == NULL) {
        goto failed;
    }

    int ret_code = response_msg->retCode;
    if (ret_code != PCRDR_SC_OK) {
        purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
        goto failed;
    }

    pcrdr_release_message(response_msg);
    return true;

failed:
    if (data != PURC_VARIANT_INVALID) {
        purc_variant_unref(data);
    }

    if (response_msg) {
        pcrdr_release_message(response_msg);
    }

    return false;
}

/* FIXME: the operation createTabbedWindow removed */
uintptr_t pcintr_rdr_create_tabbed_window(struct pcrdr_conn *conn,
        uintptr_t session, uintptr_t workspace, const char *id,
        const char *title, const char* classes, const char *style,
        const char* level)
{
    uintptr_t tabbed_window = 0;
    pcrdr_msg *response_msg = NULL;

    const char *operation = PCRDR_OPERATION_RESETPAGEGROUPS;
    pcrdr_msg_target target;
    uint64_t target_value;
    pcrdr_msg_element_type element_type = PCRDR_MSG_ELEMENT_TYPE_VOID;
    pcrdr_msg_data_type data_type = PCRDR_MSG_DATA_TYPE_EJSON;
    purc_variant_t data = PURC_VARIANT_INVALID;

    if (workspace) {
        target = PCRDR_MSG_TARGET_WORKSPACE;
        target_value = workspace;
    }
    else {
        target = PCRDR_MSG_TARGET_SESSION;
        target_value = session;
    }

    data = purc_variant_make_object(0, NULL, NULL);
    if (data == PURC_VARIANT_INVALID) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        goto failed;
    }

    if (!object_set(data, ID_KEY, id)) {
        goto failed;
    }

    if (title && !object_set(data, TITLE_KEY, title)) {
        goto failed;
    }

    if (classes && !object_set(data, CLASS_KEY, classes)) {
        goto failed;
    }

    if (style && !object_set(data, STYLE_KEY, style)) {
        goto failed;
    }

    if (level && !object_set(data, LEVEL_KEY, level)) {
        goto failed;
    }

    response_msg = pcintr_rdr_send_request_and_wait_response(conn, target,
            target_value, operation, element_type, NULL, NULL, data_type,
            data);

    if (response_msg == NULL) {
        goto failed;
    }

    int ret_code = response_msg->retCode;
    if (ret_code == PCRDR_SC_OK) {
        tabbed_window = response_msg->resultValue;
    }

    pcrdr_release_message(response_msg);

    if (ret_code != PCRDR_SC_OK) {
        purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
        goto failed;
    }

    return tabbed_window;

failed:
    if (data != PURC_VARIANT_INVALID) {
        purc_variant_unref(data);
    }

    return 0;
}

/* FIXME: the operation destroyTabbedWindow removed */
bool pcintr_rdr_destroy_tabbed_window(struct pcrdr_conn *conn,
        uintptr_t session, uintptr_t workspace, uintptr_t tabbed_window)
{
    pcrdr_msg *response_msg = NULL;

    const char *operation = PCRDR_OPERATION_REMOVEPAGEGROUP;
    pcrdr_msg_target target;
    uint64_t target_value;
    pcrdr_msg_element_type element_type = PCRDR_MSG_ELEMENT_TYPE_HANDLE;
    pcrdr_msg_data_type data_type = PCRDR_MSG_DATA_TYPE_VOID;
    purc_variant_t data = PURC_VARIANT_INVALID;

    if (workspace) {
        target = PCRDR_MSG_TARGET_WORKSPACE;
        target_value = workspace;
    }
    else {
        target = PCRDR_MSG_TARGET_SESSION;
        target_value = session;
    }

    char element[LEN_BUFF_LONGLONGINT];
    int n = snprintf(element, sizeof(element),
            "%llx", (unsigned long long int)tabbed_window);
    if (n < 0) {
        purc_set_error(PURC_ERROR_BAD_STDC_CALL);
        goto failed;
    }
    else if ((size_t)n >= sizeof (element)) {
        PC_DEBUG ("Too small elementer to serialize message.\n");
        purc_set_error(PURC_ERROR_TOO_SMALL_BUFF);
        goto failed;
    }

    response_msg = pcintr_rdr_send_request_and_wait_response(conn, target,
            target_value, operation, element_type, element, NULL,
            data_type, data);

    if (response_msg == NULL) {
        goto failed;
    }

    int ret_code = response_msg->retCode;
    if (ret_code != PCRDR_SC_OK) {
        purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
        goto failed;
    }

    pcrdr_release_message(response_msg);
    return true;

failed:
    if (data != PURC_VARIANT_INVALID) {
        purc_variant_unref(data);
    }

    if (response_msg) {
        pcrdr_release_message(response_msg);
    }

    return false;
}

/* FIXME: the operation updateTabbedWindow removed */
// property: title, class, style
bool pcintr_rdr_update_tabbed_window(struct pcrdr_conn *conn,
        uintptr_t session, uintptr_t workspace, uintptr_t tabbed_window,
        const char *property, const char *value)
{
    pcrdr_msg *response_msg = NULL;

    const char *operation = PCRDR_OPERATION_ADDPAGEGROUPS;
    pcrdr_msg_target target;
    uint64_t target_value;
    pcrdr_msg_element_type element_type = PCRDR_MSG_ELEMENT_TYPE_HANDLE;
    pcrdr_msg_data_type data_type = PCRDR_MSG_DATA_TYPE_TEXT;
    purc_variant_t data = PURC_VARIANT_INVALID;

    if (workspace) {
        target = PCRDR_MSG_TARGET_WORKSPACE;
        target_value = workspace;
    }
    else {
        target = PCRDR_MSG_TARGET_SESSION;
        target_value = session;
    }

    data = purc_variant_make_string(value, false);
    if (data == PURC_VARIANT_INVALID) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        goto failed;
    }

    char element[LEN_BUFF_LONGLONGINT];
    int n = snprintf(element, sizeof(element),
            "%llx", (unsigned long long int)tabbed_window);
    if (n < 0) {
        purc_set_error(PURC_ERROR_BAD_STDC_CALL);
        goto failed;
    }
    else if ((size_t)n >= sizeof (element)) {
        PC_DEBUG ("Too small elementer to serialize message.\n");
        purc_set_error(PURC_ERROR_TOO_SMALL_BUFF);
        goto failed;
    }

    response_msg = pcintr_rdr_send_request_and_wait_response(conn, target,
            target_value, operation, element_type, element, property,
            data_type, data);

    if (response_msg == NULL) {
        goto failed;
    }

    int ret_code = response_msg->retCode;
    if (ret_code != PCRDR_SC_OK) {
        purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
        goto failed;
    }

    pcrdr_release_message(response_msg);
    return true;

failed:
    if (data != PURC_VARIANT_INVALID) {
        purc_variant_unref(data);
    }

    if (response_msg) {
        pcrdr_release_message(response_msg);
    }

    return false;
}

uintptr_t pcintr_rdr_create_tab_page(struct pcrdr_conn *conn,
        uintptr_t tabbed_window, const char *id, const char *title
        )
{
    uintptr_t tab_page = 0;
    pcrdr_msg *response_msg = NULL;

    const char *operation = PCRDR_OPERATION_CREATEPAGE;
    pcrdr_msg_target target = PCRDR_MSG_TARGET_WORKSPACE;
    uint64_t target_value = tabbed_window;
    pcrdr_msg_element_type element_type = PCRDR_MSG_ELEMENT_TYPE_VOID;
    pcrdr_msg_data_type data_type = PCRDR_MSG_DATA_TYPE_EJSON;
    purc_variant_t data = PURC_VARIANT_INVALID;

    data = purc_variant_make_object(0, NULL, NULL);
    if (data == PURC_VARIANT_INVALID) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        goto failed;
    }

    if (!object_set(data, ID_KEY, id)) {
        goto failed;
    }

    if (title && !object_set(data, TITLE_KEY, title)) {
        goto failed;
    }

    response_msg = pcintr_rdr_send_request_and_wait_response(conn, target,
            target_value, operation, element_type, NULL, NULL, data_type,
            data);

    if (response_msg == NULL) {
        goto failed;
    }

    int ret_code = response_msg->retCode;
    if (ret_code == PCRDR_SC_OK) {
        tab_page = response_msg->resultValue;
    }

    pcrdr_release_message(response_msg);

    if (ret_code != PCRDR_SC_OK) {
        purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
        goto failed;
    }

    return tab_page;

failed:
    if (data != PURC_VARIANT_INVALID) {
        purc_variant_unref(data);
    }

    return 0;
}

bool pcintr_rdr_destroy_tab_page(struct pcrdr_conn *conn,
        uintptr_t tabbed_window, uintptr_t tab_page)
{
    pcrdr_msg *response_msg = NULL;

    const char *operation = PCRDR_OPERATION_DESTROYPAGE;
    pcrdr_msg_target target = PCRDR_MSG_TARGET_WORKSPACE;
    uint64_t target_value = tabbed_window;
    pcrdr_msg_element_type element_type = PCRDR_MSG_ELEMENT_TYPE_HANDLE;
    pcrdr_msg_data_type data_type = PCRDR_MSG_DATA_TYPE_VOID;
    purc_variant_t data = PURC_VARIANT_INVALID;

    char element[LEN_BUFF_LONGLONGINT];
    int n = snprintf(element, sizeof(element),
            "%llx", (unsigned long long int)tab_page);
    if (n < 0) {
        purc_set_error(PURC_ERROR_BAD_STDC_CALL);
        goto failed;
    }
    else if ((size_t)n >= sizeof (element)) {
        PC_DEBUG ("Too small elementer to serialize message.\n");
        purc_set_error(PURC_ERROR_TOO_SMALL_BUFF);
        goto failed;
    }

    response_msg = pcintr_rdr_send_request_and_wait_response(conn, target,
            target_value, operation, element_type, element, NULL,
            data_type, data);

    if (response_msg == NULL) {
        goto failed;
    }

    int ret_code = response_msg->retCode;
    if (ret_code != PCRDR_SC_OK) {
        purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
        goto failed;
    }

    pcrdr_release_message(response_msg);
    return true;

failed:
    if (data != PURC_VARIANT_INVALID) {
        purc_variant_unref(data);
    }

    if (response_msg) {
        pcrdr_release_message(response_msg);
    }

    return false;
}

// property: title, class, style
bool pcintr_rdr_update_tab_page(struct pcrdr_conn *conn,
        uintptr_t tabbed_window, uintptr_t tab_page,
        const char *property, const char *value)
{
    pcrdr_msg *response_msg = NULL;

    const char *operation = PCRDR_OPERATION_UPDATEPAGE;
    pcrdr_msg_target target = PCRDR_MSG_TARGET_WORKSPACE;
    uint64_t target_value = tabbed_window;
    pcrdr_msg_element_type element_type = PCRDR_MSG_ELEMENT_TYPE_HANDLE;
    pcrdr_msg_data_type data_type = PCRDR_MSG_DATA_TYPE_TEXT;
    purc_variant_t data = PURC_VARIANT_INVALID;

    data = purc_variant_make_string(value, false);
    if (data == PURC_VARIANT_INVALID) {
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        goto failed;
    }

    char element[LEN_BUFF_LONGLONGINT];
    int n = snprintf(element, sizeof(element),
            "%llx", (unsigned long long int)tab_page);
    if (n < 0) {
        purc_set_error(PURC_ERROR_BAD_STDC_CALL);
        goto failed;
    }
    else if ((size_t)n >= sizeof (element)) {
        PC_DEBUG ("Too small elementer to serialize message.\n");
        purc_set_error(PURC_ERROR_TOO_SMALL_BUFF);
        goto failed;
    }

    response_msg = pcintr_rdr_send_request_and_wait_response(conn, target,
            target_value, operation, element_type, element, property,
            data_type, data);

    if (response_msg == NULL) {
        goto failed;
    }

    int ret_code = response_msg->retCode;
    if (ret_code != PCRDR_SC_OK) {
        purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
        goto failed;
    }

    pcrdr_release_message(response_msg);
    return true;

failed:
    if (data != PURC_VARIANT_INVALID) {
        purc_variant_unref(data);
    }

    if (response_msg) {
        pcrdr_release_message(response_msg);
    }

    return false;
}

bool
purc_attach_vdom_to_renderer(purc_vdom_t vdom,
        const char *target_workspace,
        const char *target_window,
        const char *target_tabpage,
        const char *target_level,
        purc_renderer_extra_info *extra_info)
{
    if (!vdom) {
        purc_set_error(PURC_ERROR_INVALID_VALUE);
        return false;
    }

    struct pcinst *inst = pcinst_current();
    if (inst == NULL || inst->rdr_caps == NULL || target_window == NULL) {
        purc_set_error(PURC_ERROR_INVALID_VALUE);
        return false;
    }

    struct pcrdr_conn *conn_to_rdr = inst->conn_to_rdr;
    struct renderer_capabilities *rdr_caps = inst->rdr_caps;
    uintptr_t session_handle = rdr_caps->session_handle;

    uintptr_t workspace = 0;
    if (target_workspace && rdr_caps->workspace != 0) {
        workspace = pcintr_rdr_create_workspace(conn_to_rdr, session_handle,
            target_workspace,
            extra_info->workspace_title,
            extra_info->workspace_classes,
            extra_info->workspace_styles);
        if (!workspace) {
            purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
            return false;
        }
    }

    uintptr_t window = 0;
    uintptr_t tabpage = 0;
    if (target_tabpage) {
        window = pcintr_rdr_create_tabbed_window(conn_to_rdr,
            session_handle, workspace, target_window,
            extra_info->title,
            extra_info->classes,
            extra_info->styles,
            target_level);
        if (!window) {
            purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
            return false;
        }

        tabpage = pcintr_rdr_create_tab_page(conn_to_rdr, window,
                target_tabpage, extra_info->tabpage_title);
        if (!tabpage) {
            purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
            return false;
        }
    }
    else {
        window = pcintr_rdr_create_plain_window(conn_to_rdr,
            session_handle, workspace, target_window,
            extra_info->title,
            extra_info->classes,
            extra_info->styles,
            target_level);
        if (!window) {
            purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
            return false;
        }
    }

    pcvdom_document_set_target_workspace(vdom, workspace);
    pcvdom_document_set_target_window(vdom, window);
    pcvdom_document_set_target_tabpage(vdom, tabpage);

    return true;
}


bool
pcintr_rdr_page_control_load(pcintr_stack_t stack)
{
    if (!pcvdom_document_is_attached_rdr(stack->vdom)) {
        return true;
    }
    pcrdr_msg *response_msg = NULL;

    const char *operation = PCRDR_OPERATION_LOAD;
    pcrdr_msg_target target = PCRDR_MSG_TARGET_PAGE;
    uint64_t target_value;
    pcrdr_msg_element_type element_type = PCRDR_MSG_ELEMENT_TYPE_VOID;
    pcrdr_msg_data_type data_type = PCRDR_MSG_DATA_TYPE_TEXT;
    purc_variant_t req_data = PURC_VARIANT_INVALID;

    purc_vdom_t vdom = stack->vdom;
    pchtml_html_document_t *doc = stack->doc;
    int opt = 0;
    purc_rwstream_t out = NULL;

    target_value = pcvdom_document_get_target_tabpage(vdom);
    if (target_value == 0) {
        target = PCRDR_MSG_TARGET_PLAINWINDOW;
        target_value = pcvdom_document_get_target_window(vdom);
    }

    out = purc_rwstream_new_buffer(BUFF_MIN, BUFF_MAX);
    if (out == NULL) {
        goto failed;
    }

    opt |= PCHTML_HTML_SERIALIZE_OPT_UNDEF;
    opt |= PCHTML_HTML_SERIALIZE_OPT_SKIP_WS_NODES;
    opt |= PCHTML_HTML_SERIALIZE_OPT_WITHOUT_TEXT_INDENT;
    opt |= PCHTML_HTML_SERIALIZE_OPT_FULL_DOCTYPE;
    opt |= PCHTML_HTML_SERIALIZE_OPT_WITH_HVML_HANDLE;

    if(0 != pchtml_doc_write_to_stream_ex(doc, opt, out)) {
        goto failed;
    }

    size_t sz_content = 0;
    size_t sz_buff = 0;
    char* p = (char*)purc_rwstream_get_mem_buffer_ex(out, &sz_content,
            &sz_buff,true);
    req_data = purc_variant_make_string_reuse_buff(p, sz_content, false);
    if (req_data == PURC_VARIANT_INVALID) {
        free(p);
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        goto failed;
    }

    struct pcinst *inst = pcinst_current();
    response_msg = pcintr_rdr_send_request_and_wait_response(inst->conn_to_rdr,
        target, target_value, operation, element_type, NULL,
        NULL, data_type, req_data);

    if (response_msg == NULL) {
        goto failed;
    }

    int ret_code = response_msg->retCode;
    if (ret_code == PCRDR_SC_OK) {
        pcvdom_document_set_target_dom(vdom, response_msg->resultValue);
    }

    pcrdr_release_message(response_msg);
    purc_rwstream_destroy(out);
    out = NULL;

    if (ret_code != PCRDR_SC_OK) {
        purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
        goto failed;
    }

    return true;

failed:
    if (out) {
        purc_rwstream_destroy(out);
    }

    if (req_data != PURC_VARIANT_INVALID) {
        purc_variant_unref(req_data);
    }

    return false;
}

bool
pcintr_rdr_send_dom_request(pcintr_stack_t stack, const char *operation,
        pcdom_element_t *element, const char* property,
        pcrdr_msg_data_type data_type, purc_variant_t data)
{
    if (!stack || !pcvdom_document_is_attached_rdr(stack->vdom)
            || stack->stage != STACK_STAGE_EVENT_LOOP) {
        return true;
    }

    pcrdr_msg *response_msg = NULL;

    pcrdr_msg_target target = PCRDR_MSG_TARGET_DOM;
    uint64_t target_value = pcvdom_document_get_target_dom(stack->vdom);
    pcrdr_msg_element_type element_type = PCRDR_MSG_ELEMENT_TYPE_HANDLE;

    char elem[LEN_BUFF_LONGLONGINT];
    int n = snprintf(elem, sizeof(elem),
            "%llx", (unsigned long long int)(uintptr_t)element);
    if (n < 0) {
        purc_set_error(PURC_ERROR_BAD_STDC_CALL);
        goto failed;
    }
    else if ((size_t)n >= sizeof (elem)) {
        PC_DEBUG ("Too small elemer to serialize message.\n");
        purc_set_error(PURC_ERROR_TOO_SMALL_BUFF);
        goto failed;
    }

    struct pcinst *inst = pcinst_current();
    response_msg = pcintr_rdr_send_request_and_wait_response(inst->conn_to_rdr,
        target, target_value, operation, element_type, elem,
        property, data_type, data);

    if (response_msg == NULL) {
        goto failed;
    }

    int ret_code = response_msg->retCode;
    if (ret_code != PCRDR_SC_OK) {
        purc_set_error(PCRDR_ERROR_SERVER_REFUSED);
        goto failed;
    }

    pcrdr_release_message(response_msg);
    return true;

failed:
    if (response_msg != NULL) {
        pcrdr_release_message(response_msg);
    }
    return false;
}

bool
pcintr_rdr_send_dom_request_ex(pcintr_stack_t stack, const char *operation,
        pcdom_element_t *element, const char* property,
        pcrdr_msg_data_type data_type, const char* data)
{
    if (!stack || !pcvdom_document_is_attached_rdr(stack->vdom)
            || stack->stage != STACK_STAGE_EVENT_LOOP) {
        return true;
    }

    purc_variant_t req_data = PURC_VARIANT_INVALID;
    if (data_type == PCRDR_MSG_DATA_TYPE_TEXT) {
        req_data = purc_variant_make_string(data, false);
        if (req_data == PURC_VARIANT_INVALID) {
            purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
            goto failed;
        }
    }
    else if (data_type == PCRDR_MSG_DATA_TYPE_EJSON) {
        req_data = purc_variant_make_from_json_string(data, strlen(data));
        if (req_data == PURC_VARIANT_INVALID) {
            purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
            goto failed;
        }
    }

    return pcintr_rdr_send_dom_request(stack, operation, element,
            property, data_type, req_data);

failed:
    if (req_data != PURC_VARIANT_INVALID) {
        purc_variant_unref(req_data);
    }
    return false;
}

static purc_variant_t
serialize_node(pcdom_node_t *node)
{
    purc_rwstream_t out = purc_rwstream_new_buffer(BUFF_MIN, BUFF_MAX);
    if (out == NULL) {
        goto failed;
    }

    int opt = 0;
    opt |= PCHTML_HTML_SERIALIZE_OPT_UNDEF;
    opt |= PCHTML_HTML_SERIALIZE_OPT_SKIP_WS_NODES;
    opt |= PCHTML_HTML_SERIALIZE_OPT_WITHOUT_TEXT_INDENT;
    opt |= PCHTML_HTML_SERIALIZE_OPT_FULL_DOCTYPE;
    opt |= PCHTML_HTML_SERIALIZE_OPT_WITH_HVML_HANDLE;

    if(0 != pcdom_node_write_to_stream_ex(node, opt, out)) {
        goto failed;
    }

    size_t sz_content = 0;
    size_t sz_buff = 0;
    char* p = (char*)purc_rwstream_get_mem_buffer_ex(out, &sz_content,
            &sz_buff,true);
    purc_variant_t v = purc_variant_make_string_reuse_buff(p,
            sz_content, false);
    if (v == PURC_VARIANT_INVALID) {
        free(p);
        purc_set_error(PURC_ERROR_OUT_OF_MEMORY);
        goto failed;
    }

    return v;

failed:
    return PURC_VARIANT_INVALID;
}

bool
pcintr_rdr_dom_append_child(pcintr_stack_t stack, pcdom_element_t *element,
        pcdom_node_t *child)
{
    if (!stack || !pcvdom_document_is_attached_rdr(stack->vdom)
            || stack->stage != STACK_STAGE_EVENT_LOOP) {
        return true;
    }
    purc_variant_t data = serialize_node(child);
    if (data == PURC_VARIANT_INVALID) {
        return false;
    }

    return pcintr_rdr_send_dom_request(stack, PCRDR_OPERATION_APPEND,
            element, NULL, PCRDR_MSG_DATA_TYPE_TEXT, data);
}

bool
pcintr_rdr_dom_displace_child(pcintr_stack_t stack, pcdom_element_t *element,
        pcdom_node_t *child)
{
    if (!stack || !pcvdom_document_is_attached_rdr(stack->vdom)
            || stack->stage != STACK_STAGE_EVENT_LOOP) {
        return true;
    }

    purc_variant_t data = serialize_node(child);
    if (data == PURC_VARIANT_INVALID) {
        return false;
    }

    return pcintr_rdr_send_dom_request(stack, PCRDR_OPERATION_DISPLACE,
            element, NULL, PCRDR_MSG_DATA_TYPE_TEXT, data);
}

