#ifndef HBS_TEST_H
#define HBS_TEST_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int _tests_run;
extern int _tests_passed;
extern int _tests_failed;

#define ASSERT_STR_EQ(expected, actual, msg) do { \
    _tests_run++; \
    if ((expected) == NULL && (actual) == NULL) { \
        _tests_passed++; \
        printf("  PASS: %s\n", msg); \
    } else if ((expected) && (actual) && strcmp((expected), (actual)) == 0) { \
        _tests_passed++; \
        printf("  PASS: %s\n", msg); \
    } else { \
        _tests_failed++; \
        printf("  FAIL: %s\n", msg); \
        printf("    expected: \"%s\"\n", (expected) ? (expected) : "(null)"); \
        printf("    actual:   \"%s\"\n", (actual) ? (actual) : "(null)"); \
    } \
} while (0)

#define ASSERT_NOT_NULL(ptr, msg) do { \
    _tests_run++; \
    if ((ptr) != NULL) { \
        _tests_passed++; \
        printf("  PASS: %s\n", msg); \
    } else { \
        _tests_failed++; \
        printf("  FAIL: %s (was NULL)\n", msg); \
    } \
} while (0)

#define ASSERT_NULL(ptr, msg) do { \
    _tests_run++; \
    if ((ptr) == NULL) { \
        _tests_passed++; \
        printf("  PASS: %s\n", msg); \
    } else { \
        _tests_failed++; \
        printf("  FAIL: %s (was not NULL)\n", msg); \
    } \
} while (0)

#define TEST_SUITE(name) printf("\n=== %s ===\n", name)

#define TEST_SUMMARY() do { \
    printf("\n---\nResults: %d/%d passed", _tests_passed, _tests_run); \
    if (_tests_failed > 0) printf(" (%d FAILED)", _tests_failed); \
    printf("\n"); \
} while (0)

/* Helper: compile template, render with context, return result string */
static inline char *render_template(hbs_env_t *env, const char *source, json_object *ctx) {
    hbs_error_t err;
    hbs_template_t *tmpl = hbs_compile(env, source, &err);
    if (!tmpl) return NULL;
    char *result = hbs_render(tmpl, ctx, &err);
    hbs_template_destroy(tmpl);
    return result;
}

#endif /* HBS_TEST_H */
