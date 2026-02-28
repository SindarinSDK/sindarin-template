#include "handlebars.h"
#include "test.h"

int _tests_run = 0;
int _tests_passed = 0;
int _tests_failed = 0;

/* Test suite declarations */
void test_expressions(void);
void test_helpers(void);
void test_escaping(void);
void test_partials(void);
void test_blocks(void);

int main(void) {
    printf("Sindarin Template Engine - Test Suite\n");
    printf("=====================================\n");

    test_expressions();
    test_helpers();
    test_escaping();
    test_partials();
    test_blocks();

    TEST_SUMMARY();

    return _tests_failed > 0 ? 1 : 0;
}
