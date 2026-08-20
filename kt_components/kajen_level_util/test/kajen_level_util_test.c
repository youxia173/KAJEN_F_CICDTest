#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>

#include "kajen_level_util.h"

static void test_level254_to_percent(void **state)
{
  (void)state;
  assert_int_equal(kajen_level254_to_percent(0), 0);
  assert_int_equal(kajen_level254_to_percent(1), 0);
  assert_int_equal(kajen_level254_to_percent(127), 50);
  assert_int_equal(kajen_level254_to_percent(254), 100);
}

static void test_clamp_u16(void **state)
{
  (void)state;
  assert_int_equal(kajen_clamp_u16(5, 10, 20), 10);
  assert_int_equal(kajen_clamp_u16(15, 10, 20), 15);
  assert_int_equal(kajen_clamp_u16(25, 10, 20), 20);
}

static void test_permille_to_percent(void **state)
{
  (void)state;
  assert_int_equal(kajen_permille_to_percent(0), 0);
  assert_int_equal(kajen_permille_to_percent(500), 50);
  assert_int_equal(kajen_permille_to_percent(1000), 100);
  assert_int_equal(kajen_permille_to_percent(1500), 100);
}

static void test_validate_permille(void **state)
{
  (void)state;
  assert_int_equal(kajen_validate_permille(0), KAJEN_SUCCESS);
  assert_int_equal(kajen_validate_permille(1000), KAJEN_SUCCESS);
  assert_int_equal(kajen_validate_permille(1001), KAJEN_ERROR_BAD_ARGUMENT);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_level254_to_percent),
      cmocka_unit_test(test_clamp_u16),
      cmocka_unit_test(test_permille_to_percent),
      cmocka_unit_test(test_validate_permille),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
