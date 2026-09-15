#include "unity/unity.h"

// required by unity
void setUp() {}
void tearDown() {}

void test_unity(void)
{
  TEST_ASSERT(2 + 2 == 4);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_unity);
  return UNITY_END();
}
