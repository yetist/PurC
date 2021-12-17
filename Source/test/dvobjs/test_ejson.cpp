#include "purc.h"
#include "purc-variant.h"
#include "private/avl.h"
#include "private/hashtable.h"
#include "private/variant.h"
#include "private/errors.h"
#include "private/debug.h"
#include "private/utils.h"
#include "private/dvobjs.h"

#include "../helpers.h"

#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <gtest/gtest.h>

extern purc_variant_t get_variant (char *buf, size_t *length);
extern void get_variant_total_info (size_t *mem, size_t *value, size_t *resv);
#define MAX_PARAM_NR    20

TEST(dvobjs, dvobjs_ejson_type)
{
    const char *function[] = {"type"};
    purc_variant_t param[MAX_PARAM_NR] = {0};
    purc_variant_t ret_var = PURC_VARIANT_INVALID;
    purc_variant_t ret_result = PURC_VARIANT_INVALID;
    size_t function_size = PCA_TABLESIZE(function);
    size_t i = 0;
    size_t line_number = 0;
    size_t sz_total_mem_before = 0;
    size_t sz_total_values_before = 0;
    size_t nr_reserved_before = 0;
    size_t sz_total_mem_after = 0;
    size_t sz_total_values_after = 0;
    size_t nr_reserved_after = 0;
    char file_path[1024];
    char data_path[PATH_MAX+1];
    const char *env = "DVOBJS_TEST_PATH";
    test_getpath_from_env_or_rel(data_path, sizeof(data_path),
        env, "test_files");
    std::cerr << "env: " << env << "=" << data_path << std::endl;

    // get and function
    purc_instance_extra_info info = {};
    int ret = purc_init ("cn.fmsoft.hybridos.test", "test_init", &info);
    ASSERT_EQ (ret, PURC_ERROR_OK);

    purc_variant_t ejson = pcdvobjs_get_ejson();
    ASSERT_NE(ejson, nullptr);
    ASSERT_EQ(purc_variant_is_object (ejson), true);

    for (i = 0; i < function_size; i++) {
        printf ("test _L.%s:\n", function[i]);

        purc_variant_t dynamic = purc_variant_object_get_by_ckey (ejson,
                function[i]);
        ASSERT_NE(dynamic, nullptr);
        ASSERT_EQ(purc_variant_is_dynamic (dynamic), true);

        purc_dvariant_method func = NULL;
        func = purc_variant_dynamic_get_getter (dynamic);
        ASSERT_NE(func, nullptr);

        // get test file
        strcpy (file_path, data_path);
        strcat (file_path, "/");
        strcat (file_path, function[i]);
        strcat (file_path, ".test");

        FILE *fp = fopen(file_path, "r");   // open test_list
        ASSERT_NE(fp, nullptr) << "Failed to open file: ["
                               << file_path
                               << "]"
                               << std::endl;

        char *line = NULL;
        size_t sz = 0;
        ssize_t read = 0;
        size_t j = 0;
        size_t length_sub = 0;

        line_number = 0;

        get_variant_total_info (&sz_total_mem_before, &sz_total_values_before,
                &nr_reserved_before);

        while ((read = getline(&line, &sz, fp)) != -1) {
            *(line + read - 1) = 0;
            line_number ++;

            if (strncasecmp (line, "test_begin", 10) == 0) {
                printf ("\ttest case on line %ld\n", line_number);

                // get parameters
                read = getline(&line, &sz, fp);
                *(line + read - 1) = 0;
                line_number ++;

                if (strcmp (line, "param_begin") == 0) {
                    j = 0;

                    // get param
                    while (1) {
                        read = getline(&line, &sz, fp);
                        *(line + read - 1) = 0;
                        line_number ++;

                        if (strcmp (line, "param_end") == 0) {
                            break;
                        }
                        param[j] = get_variant (line, &length_sub);
                        j++;
                        ASSERT_LE(j, MAX_PARAM_NR); 
                    }

                    // get result
                    read = getline(&line, &sz, fp);
                    *(line + read - 1) = 0;
                    line_number ++;

                    ret_result = get_variant(line, &length_sub);

                    // test case end
                    while (1) {
                        read = getline(&line, &sz, fp);
                        *(line + read - 1) = 0;
                        line_number ++;

                        if (strcmp (line, "test_end") == 0) {
                            break;
                        }
                    }

                    ret_var = func (NULL, j, param);

                    if (ret_result == PURC_VARIANT_INVALID) {
                        ASSERT_EQ(ret_var, PURC_VARIANT_INVALID);
                    } else {
                        // USER MODIFIED HERE.
                        ASSERT_EQ(purc_variant_is_type (ret_var,
                                    PURC_VARIANT_TYPE_STRING), true);
                        ASSERT_STREQ(purc_variant_get_string_const (ret_var),
                                purc_variant_get_string_const (ret_result));
                    }
                    if (ret_var != PURC_VARIANT_INVALID) {
                        purc_variant_unref(ret_var);
                        ret_var = PURC_VARIANT_INVALID;
                    }

                    if (ret_result != PURC_VARIANT_INVALID) {
                        purc_variant_unref(ret_result);
                        ret_result = PURC_VARIANT_INVALID;
                    }

                    for (size_t i = 0; i < j; ++i) {
                        if (param[i] != PURC_VARIANT_INVALID) {
                            purc_variant_unref(param[i]);
                            param[i] = PURC_VARIANT_INVALID;
                        }
                    }

                    get_variant_total_info (&sz_total_mem_after,
                            &sz_total_values_after, &nr_reserved_after);
                    ASSERT_EQ(sz_total_values_before, sz_total_values_after);
                    ASSERT_EQ(sz_total_mem_after,
                            sz_total_mem_before + (nr_reserved_after -
                                nr_reserved_before) * sizeof(purc_variant));

                } else
                    continue;
            } else
                continue;
        }

        length_sub++;
        fclose(fp);
        if (line)
            free(line);
    }

    purc_variant_unref(ejson);
    purc_cleanup ();
}

TEST(dvobjs, dvobjs_ejson_number)
{
    const char *function[] = {"count"};
    purc_variant_t param[MAX_PARAM_NR] = {0};
    purc_variant_t ret_var = PURC_VARIANT_INVALID;
    purc_variant_t ret_result = PURC_VARIANT_INVALID;
    size_t function_size = PCA_TABLESIZE(function);;
    size_t i = 0;
    size_t line_number = 0;
    size_t sz_total_mem_before = 0;
    size_t sz_total_values_before = 0;
    size_t nr_reserved_before = 0;
    size_t sz_total_mem_after = 0;
    size_t sz_total_values_after = 0;
    size_t nr_reserved_after = 0;
    char file_path[1024];
    char data_path[PATH_MAX+1];
    const char *env = "DVOBJS_TEST_PATH";
    test_getpath_from_env_or_rel(data_path, sizeof(data_path),
        env, "test_files");
    std::cerr << "env: " << env << "=" << data_path << std::endl;

    // get and function
    purc_instance_extra_info info = {};
    int ret = purc_init ("cn.fmsoft.hybridos.test", "test_init", &info);
    ASSERT_EQ (ret, PURC_ERROR_OK);

    purc_variant_t ejson = pcdvobjs_get_ejson();
    ASSERT_NE(ejson, nullptr);
    ASSERT_EQ(purc_variant_is_object (ejson), true);

    for (i = 0; i < function_size; i++) {
        printf ("test _L.%s:\n", function[i]);

        purc_variant_t dynamic = purc_variant_object_get_by_ckey (ejson,
                function[i]);
        ASSERT_NE(dynamic, nullptr);
        ASSERT_EQ(purc_variant_is_dynamic (dynamic), true);

        purc_dvariant_method func = NULL;
        func = purc_variant_dynamic_get_getter (dynamic);
        ASSERT_NE(func, nullptr);

        // get test file
        strcpy (file_path, data_path);
        strcat (file_path, "/");
        strcat (file_path, function[i]);
        strcat (file_path, ".test");

        FILE *fp = fopen(file_path, "r");   // open test_list
        ASSERT_NE(fp, nullptr) << "Failed to open file: ["
                               << file_path
                               << "]"
                               << std::endl;

        char *line = NULL;
        size_t sz = 0;
        ssize_t read = 0;
        size_t j = 0;
        size_t length_sub = 0;

        line_number = 0;

        get_variant_total_info (&sz_total_mem_before, &sz_total_values_before,
                &nr_reserved_before);

        while ((read = getline(&line, &sz, fp)) != -1) {
            *(line + read - 1) = 0;
            line_number ++;

            if (strncasecmp (line, "test_begin", 10) == 0) {
                printf ("\ttest case on line %ld\n", line_number);

                // get parameters
                read = getline(&line, &sz, fp);
                *(line + read - 1) = 0;
                line_number ++;

                if (strcmp (line, "param_begin") == 0) {
                    j = 0;

                    // get param
                    while (1) {
                        read = getline(&line, &sz, fp);
                        *(line + read - 1) = 0;
                        line_number ++;

                        if (strcmp (line, "param_end") == 0) {
                            break;
                        }
                        param[j] = get_variant (line, &length_sub);
                        j++;
                        ASSERT_LE(j, MAX_PARAM_NR); 
                    }

                    // get result
                    read = getline(&line, &sz, fp);
                    *(line + read - 1) = 0;
                    line_number ++;

                    ret_result = get_variant(line, &length_sub);

                    // test case end
                    while (1) {
                        read = getline(&line, &sz, fp);
                        *(line + read - 1) = 0;
                        line_number ++;

                        if (strcmp (line, "test_end") == 0) {
                            break;
                        }
                    }

                    ret_var = func (NULL, j, param);

                    if (ret_result == PURC_VARIANT_INVALID) {
                        ASSERT_EQ(ret_var, PURC_VARIANT_INVALID);
                    } else {
                        // USER MODIFIED HERE.
                        ASSERT_EQ(purc_variant_is_type (ret_var,
                                    PURC_VARIANT_TYPE_ULONGINT), true);
                        ASSERT_EQ(ret_var->u64, ret_result->u64);
                    }
                    if (ret_var != PURC_VARIANT_INVALID) {
                        purc_variant_unref(ret_var);
                        ret_var = PURC_VARIANT_INVALID;
                    }

                    if (ret_result != PURC_VARIANT_INVALID) {
                        purc_variant_unref(ret_result);
                        ret_result = PURC_VARIANT_INVALID;
                    }

                    for (size_t i = 0; i < j; ++i) {
                        if (param[i] != PURC_VARIANT_INVALID) {
                            purc_variant_unref(param[i]);
                            param[i] = PURC_VARIANT_INVALID;
                        }
                    }

                    get_variant_total_info (&sz_total_mem_after,
                            &sz_total_values_after, &nr_reserved_after);
                    ASSERT_EQ(sz_total_values_before, sz_total_values_after);
                    ASSERT_EQ(sz_total_mem_after,
                            sz_total_mem_before + (nr_reserved_after -
                                nr_reserved_before) * sizeof(purc_variant));
                } else
                    continue;
            } else
                continue;
        }

        length_sub++;
        fclose(fp);
        if (line)
            free(line);
    }

    purc_variant_unref(ejson);
    purc_cleanup ();
}

TEST(dvobjs, dvobjs_ejson_compare)
{
    const char *function[] = {"compare"};
    purc_variant_t param[MAX_PARAM_NR] = {0};
    purc_variant_t ret_var = PURC_VARIANT_INVALID;
    purc_variant_t ret_result = PURC_VARIANT_INVALID;
    size_t function_size = PCA_TABLESIZE(function);;
    size_t i = 0;
    size_t line_number = 0;
    size_t sz_total_mem_before = 0;
    size_t sz_total_values_before = 0;
    size_t nr_reserved_before = 0;
    size_t sz_total_mem_after = 0;
    size_t sz_total_values_after = 0;
    size_t nr_reserved_after = 0;
    char file_path[1024];
    char data_path[PATH_MAX+1];
    const char *env = "DVOBJS_TEST_PATH";
    test_getpath_from_env_or_rel(data_path, sizeof(data_path),
        env, "test_files");
    std::cerr << "env: " << env << "=" << data_path << std::endl;

    // get and function
    purc_instance_extra_info info = {};
    int ret = purc_init ("cn.fmsoft.hybridos.test", "test_init", &info);
    ASSERT_EQ (ret, PURC_ERROR_OK);

    purc_variant_t ejson = pcdvobjs_get_ejson();
    ASSERT_NE(ejson, nullptr);
    ASSERT_EQ(purc_variant_is_object (ejson), true);

    for (i = 0; i < function_size; i++) {
        printf ("test _L.%s:\n", function[i]);

        purc_variant_t dynamic = purc_variant_object_get_by_ckey (ejson,
                function[i]);
        ASSERT_NE(dynamic, nullptr);
        ASSERT_EQ(purc_variant_is_dynamic (dynamic), true);

        purc_dvariant_method func = NULL;
        func = purc_variant_dynamic_get_getter (dynamic);
        ASSERT_NE(func, nullptr);

        // get test file
        strcpy (file_path, data_path);
        strcat (file_path, "/");
        strcat (file_path, function[i]);
        strcat (file_path, ".test");

        FILE *fp = fopen(file_path, "r");   // open test_list
        ASSERT_NE(fp, nullptr) << "Failed to open file: ["
                               << file_path
                               << "]"
                               << std::endl;

        char *line = NULL;
        size_t sz = 0;
        ssize_t read = 0;
        size_t j = 0;
        size_t length_sub = 0;

        line_number = 0;

        get_variant_total_info (&sz_total_mem_before, &sz_total_values_before,
                &nr_reserved_before);

        while ((read = getline(&line, &sz, fp)) != -1) {
            *(line + read - 1) = 0;
            line_number ++;

            if (strncasecmp (line, "test_begin", 10) == 0) {
                printf ("\ttest case on line %ld\n", line_number);

                // get parameters
                read = getline(&line, &sz, fp);
                *(line + read - 1) = 0;
                line_number ++;

                if (strcmp (line, "param_begin") == 0) {
                    j = 0;

                    // get param
                    while (1) {
                        read = getline(&line, &sz, fp);
                        *(line + read - 1) = 0;
                        line_number ++;

                        if (strcmp (line, "param_end") == 0) {
                            break;
                        }
                        param[j] = get_variant (line, &length_sub);
                        j++;
                        ASSERT_LE(j, MAX_PARAM_NR);
                    }

                    // get result
                    read = getline(&line, &sz, fp);
                    *(line + read - 1) = 0;
                    line_number ++;

                    ret_result = get_variant(line, &length_sub);

                    // test case end
                    while (1) {
                        read = getline(&line, &sz, fp);
                        *(line + read - 1) = 0;
                        line_number ++;

                        if (strcmp (line, "test_end") == 0) {
                            break;
                        }
                    }

                    ret_var = func (NULL, j, param);

                    if (ret_result == PURC_VARIANT_INVALID) {
                        ASSERT_EQ(ret_var, PURC_VARIANT_INVALID);
                    } else {
                        // USER MODIFIED HERE.
                        ASSERT_EQ(purc_variant_is_type (ret_var,
                                    PURC_VARIANT_TYPE_NUMBER), true);
                        if (ret_result->d == 0)
                            ASSERT_EQ(ret_var->d, 0);
                        else
                            ASSERT_GT(ret_var->d * ret_result->d, 0);
                    }
                    if (ret_var != PURC_VARIANT_INVALID) {
                        purc_variant_unref(ret_var);
                        ret_var = PURC_VARIANT_INVALID;
                    }

                    if (ret_result != PURC_VARIANT_INVALID) {
                        purc_variant_unref(ret_result);
                        ret_result = PURC_VARIANT_INVALID;
                    }

                    for (size_t i = 0; i < j; ++i) {
                        if (param[i] != PURC_VARIANT_INVALID) {
                            purc_variant_unref(param[i]);
                            param[i] = PURC_VARIANT_INVALID;
                        }
                    }

                    get_variant_total_info (&sz_total_mem_after,
                            &sz_total_values_after, &nr_reserved_after);
                    ASSERT_EQ(sz_total_values_before, sz_total_values_after);
                    ASSERT_EQ(sz_total_mem_after,
                            sz_total_mem_before + (nr_reserved_after -
                                nr_reserved_before) * sizeof(purc_variant));
                } else
                    continue;
            } else
                continue;
        }

        length_sub++;
        fclose(fp);
        if (line)
            free(line);
    }

    purc_variant_unref(ejson);
    purc_cleanup ();
}

TEST(dvobjs, dvobjs_ejson_sort)
{
    const char *function[] = {"sort"};
    purc_variant_t param[MAX_PARAM_NR] = {0};
    purc_variant_t ret_var = PURC_VARIANT_INVALID;
    purc_variant_t ret_result = PURC_VARIANT_INVALID;
    size_t function_size = PCA_TABLESIZE(function);;
    size_t i = 0;
    size_t line_number = 0;
    size_t sz_total_mem_before = 0;
    size_t sz_total_values_before = 0;
    size_t nr_reserved_before = 0;
    size_t sz_total_mem_after = 0;
    size_t sz_total_values_after = 0;
    size_t nr_reserved_after = 0;
    char file_path[1024];
    char data_path[PATH_MAX+1];
    const char *env = "DVOBJS_TEST_PATH";
    test_getpath_from_env_or_rel(data_path, sizeof(data_path),
        env, "test_files");
    std::cerr << "env: " << env << "=" << data_path << std::endl;

    // get and function
    purc_instance_extra_info info = {};
    int ret = purc_init ("cn.fmsoft.hybridos.test", "test_init", &info);
    ASSERT_EQ (ret, PURC_ERROR_OK);

    purc_variant_t ejson = pcdvobjs_get_ejson();
    ASSERT_NE(ejson, nullptr);
    ASSERT_EQ(purc_variant_is_object (ejson), true);

    for (i = 0; i < function_size; i++) {
        printf ("test _L.%s:\n", function[i]);

        purc_variant_t dynamic = purc_variant_object_get_by_ckey (ejson,
                function[i]);
        ASSERT_NE(dynamic, nullptr);
        ASSERT_EQ(purc_variant_is_dynamic (dynamic), true);

        purc_dvariant_method func = NULL;
        func = purc_variant_dynamic_get_getter (dynamic);
        ASSERT_NE(func, nullptr);

        // get test file
        strcpy (file_path, data_path);
        strcat (file_path, "/");
        strcat (file_path, function[i]);
        strcat (file_path, ".test");

        FILE *fp = fopen(file_path, "r");   // open test_list
        ASSERT_NE(fp, nullptr) << "Failed to open file: ["
                               << file_path
                               << "]"
                               << std::endl;

        char *line = NULL;
        size_t sz = 0;
        ssize_t read = 0;
        size_t j = 0;
        size_t length_sub = 0;

        line_number = 0;

        get_variant_total_info (&sz_total_mem_before, &sz_total_values_before,
                &nr_reserved_before);

        while ((read = getline(&line, &sz, fp)) != -1) {
            *(line + read - 1) = 0;
            line_number ++;

            if (strncasecmp (line, "test_begin", 10) == 0) {
                printf ("\ttest case on line %ld\n", line_number);

                // get parameters
                read = getline(&line, &sz, fp);
                *(line + read - 1) = 0;
                line_number ++;

                if (strcmp (line, "param_begin") == 0) {
                    j = 0;

                    // get param
                    while (1) {
                        read = getline(&line, &sz, fp);
                        *(line + read - 1) = 0;
                        line_number ++;

                        if (strcmp (line, "param_end") == 0) {
                            break;
                        }
                        param[j] = get_variant (line, &length_sub);
                        j++;
                        ASSERT_LE(j, MAX_PARAM_NR);
                    }

                    // get result
                    read = getline(&line, &sz, fp);
                    *(line + read - 1) = 0;
                    line_number ++;

                    ret_result = get_variant(line, &length_sub);

                    // test case end
                    while (1) {
                        read = getline(&line, &sz, fp);
                        *(line + read - 1) = 0;
                        line_number ++;

                        if (strcmp (line, "test_end") == 0) {
                            break;
                        }
                    }

                    ret_var = func (NULL, j, param);

                    if (ret_result == PURC_VARIANT_INVALID) {
                        ASSERT_EQ(ret_var, PURC_VARIANT_INVALID);
                    } else {
                        // USER MODIFIED HERE.
                        char *buf1 = NULL;
                        char *buf2 = NULL;

                        purc_variant_stringify_alloc (&buf1, ret_var);
                        purc_variant_stringify_alloc (&buf2, ret_var);
                        ASSERT_STREQ (buf1, buf2);

                        free (buf1);
                        free (buf2);
                    }

                    // it will return array or set variant. do not unref here
//                    if (ret_var != PURC_VARIANT_INVALID) {
//                        purc_variant_unref(ret_var);
//                        ret_var = PURC_VARIANT_INVALID;
//                    }

                    if (ret_result != PURC_VARIANT_INVALID) {
                        purc_variant_unref(ret_result);
                        ret_result = PURC_VARIANT_INVALID;
                    }

                    for (size_t i = 0; i < j; ++i) {
                        if (param[i] != PURC_VARIANT_INVALID) {
                            purc_variant_unref(param[i]);
                            param[i] = PURC_VARIANT_INVALID;
                        }
                    }

                    get_variant_total_info (&sz_total_mem_after,
                            &sz_total_values_after, &nr_reserved_after);
                    ASSERT_EQ(sz_total_values_before, sz_total_values_after);
                    ASSERT_EQ(sz_total_mem_after,
                            sz_total_mem_before + (nr_reserved_after -
                                nr_reserved_before) * sizeof(purc_variant));
                } else
                    continue;
            } else
                continue;
        }

        length_sub++;
        fclose(fp);
        if (line)
            free(line);
    }

    purc_variant_unref(ejson);
    purc_cleanup ();
}

