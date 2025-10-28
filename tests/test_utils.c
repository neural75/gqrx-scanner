/*
MIT License

Copyright (c) 2017 neural75

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdbool.h>

/* Test prefix function */
bool prefix(const char *pre, const char *str)
{
    return strncmp(pre, str, strlen(pre)) == 0;
}

static void test_prefix_match(void **state)
{
    (void) state;
    assert_true(prefix("Hello", "Hello World"));
    assert_true(prefix("# ", "# Comment"));
}

static void test_prefix_no_match(void **state)
{
    (void) state;
    assert_false(prefix("Goodbye", "Hello World"));
    assert_false(prefix("Test", "# Comment"));
}

static void test_prefix_empty(void **state)
{
    (void) state;
    assert_true(prefix("", "Any String"));
    assert_true(prefix("", ""));
}

static void test_prefix_exact_match(void **state)
{
    (void) state;
    assert_true(prefix("Match", "Match"));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_prefix_match),
        cmocka_unit_test(test_prefix_no_match),
        cmocka_unit_test(test_prefix_empty),
        cmocka_unit_test(test_prefix_exact_match),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
