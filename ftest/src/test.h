#ifndef TEST_H_FTEST
#define TEST_H_FTEST
#include <stdio.h>
#include <sys/time.h>

#define FTEST(name) name##_test()
#define FUNIT_TEST(name) FTEST(name)
#define FUNIT(name) void FUNIT_TEST(name)

#define FTEST_START(name)                                                           \
    void FTEST(name)                                                                \
    {                                                                               \
        bool is_test_failed___ = false;                                             \
        static char test_name___[] = #name;                                         \
        struct timeval test_t1___;                                                  \
        struct timeval test_t2___;                                                  \
        gettimeofday(&test_t1___, 0);                                               \
        printf("[ RUN      ] %s\n", test_name___);

#define FTEST_END()                                                                 \
    goto test_end___;                                                               \
    test_end___:                                                                    \
        gettimeofday(&test_t2___, 0);                                               \
        double elapsed_time___ = (test_t2___.tv_sec - test_t1___.tv_sec) * 1000.0;  \
        elapsed_time___ += (test_t2___.tv_usec - test_t1___.tv_usec) / 1000.0;      \
        char const *status_str___ = is_test_failed___ ? "  FAILED" : "      OK";    \
        printf("[ %s ] %s (%0lu ms)\n",                                             \
            status_str___,                                                          \
            test_name___,                                                           \
            (unsigned long)elapsed_time___);                                        \
    }

#define FTEST_ASSERT(expr)                                                          \
    if (!(expr))                                                                    \
    {                                                                               \
        printf("%s:%d Expression: '%s' failed\n", __FILE__, __LINE__, #expr);       \
        is_test_failed___ = true;                                                   \
        goto test_end___;                                                           \
    }

#define FUNIT_TEST_START(name)                                                      \
    void FUNIT_TEST(name)                                                           \
    {                                                                               \
        static char test_name___[] = #name;                                         \
        struct timeval test_t1___;                                                  \
        gettimeofday(&test_t1___, 0);                                               \
        printf("[----------] %s\n", test_name___);

#define FUNIT_TEST_END()                                                            \
        struct timeval test_t2___;                                                  \
        gettimeofday(&test_t2___, 0);                                               \
        double elapsed_time___ = (test_t2___.tv_sec - test_t1___.tv_sec) * 1000.0;  \
        elapsed_time___ += (test_t2___.tv_usec - test_t1___.tv_usec) / 1000.0;      \
        printf("[----------] %s (%0lu ms)\n", test_name___, (unsigned long)elapsed_time___);\
    }

#endif
