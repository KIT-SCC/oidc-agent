#include "tc_toBase64UrlSafe.h"

#include "utils/crypt/crypt.h"
#include "utils/memory.h"
#include "utils/oidc_error.h"

START_TEST(test_NULL) {
  ck_assert_ptr_eq(toBase64UrlSafe(NULL, 5), NULL);
  ck_assert_int_eq(oidc_errno, OIDC_EARGNULLFUNC);
}
END_TEST

START_TEST(test_encode) {
  char* s = toBase64UrlSafe("test", 4);
  ck_assert_ptr_ne(s, NULL);
  ck_assert_str_eq(s, "dGVzdA");
  secFree(s);
}
END_TEST

TCase* test_case_toBase64UrlSafe() {
  TCase* tc = tcase_create("toBase64UrlSafe");
  tcase_add_test(tc, test_NULL);
  tcase_add_test(tc, test_encode);
  return tc;
}
