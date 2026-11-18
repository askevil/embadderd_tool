#ifndef PCFG_TEST_HARNESS_H
#define PCFG_TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(condition)                                                   \
    do                                                                           \
    {                                                                            \
        if (!(condition))                                                        \
        {                                                                        \
            fprintf(stderr, "%s:%d: assertion failed: %s\n",                   \
                    __FILE__, __LINE__, #condition);                             \
            exit(EXIT_FAILURE);                                                  \
        }                                                                        \
    } while (0)

#define TEST_ASSERT_EQ_U32(expected, actual)                                     \
    do                                                                           \
    {                                                                            \
        unsigned long test_expected_ = (unsigned long)(expected);                \
        unsigned long test_actual_ = (unsigned long)(actual);                    \
        if (test_expected_ != test_actual_)                                      \
        {                                                                        \
            fprintf(stderr, "%s:%d: expected 0x%08lx, got 0x%08lx\n",          \
                    __FILE__, __LINE__, test_expected_, test_actual_);            \
            exit(EXIT_FAILURE);                                                  \
        }                                                                        \
    } while (0)

#define TEST_ASSERT_MEM_EQ(expected, actual, size)                               \
    do                                                                           \
    {                                                                            \
        if (memcmp((expected), (actual), (size)) != 0)                           \
        {                                                                        \
            fprintf(stderr, "%s:%d: memory comparison failed\n",               \
                    __FILE__, __LINE__);                                         \
            exit(EXIT_FAILURE);                                                  \
        }                                                                        \
    } while (0)

#define RUN_TEST(function)                                                       \
    do                                                                           \
    {                                                                            \
        printf("[ RUN      ] %s\n", #function);                                 \
        function();                                                              \
        printf("[       OK ] %s\n", #function);                                 \
    } while (0)

#endif
