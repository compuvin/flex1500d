// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_TEST_ASSERT_H
#define FLEX1500_TEST_ASSERT_H

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,         \
                    __LINE__, #condition);                                     \
            abort();                                                           \
        }                                                                      \
    } while (0)

#endif
