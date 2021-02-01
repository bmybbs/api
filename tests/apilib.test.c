#include <arpa/inet.h>
#include <check.h>
#include "ytht/fileop.h"
#include "bmy/convcode.h"
#include "../apilib.h"

#define DELIMITER "\xFF"

extern char *parse_article_js_internal(struct mmapfile *pmf, struct attach_link **attach_link_list, const char *bname, const char *fname);

START_TEST(parse_article_js_internal_plain_content) {
	char *s = "AUTHOR BOARD\n"
		"TITLE\n"
		"SITE\n"
		"\n"
		"foo\n"
		"--\n"
		"FROM";
	struct mmapfile mf = { .ptr = s, .size = strlen(s) };
	struct attach_link *root = NULL;

	char *result = parse_article_js_internal(&mf, &root, "", "");
	ck_assert_ptr_nonnull(result);
	ck_assert_ptr_null(root);
	ck_assert_str_eq(result, "foo\n");

	free(result);
}

START_TEST(parse_article_js_internal_plain_content_with_ansi) {
	char *s = "AUTHOR BOARD\n"
		"TITLE\n"
		"SITE\n"
		"\n"
		"\033[1;31mfoo\033[0m\n"
		"--\n"
		"FROM";
	struct mmapfile mf = { .ptr = s, .size = strlen(s) };
	struct attach_link *root = NULL;

	char *result = parse_article_js_internal(&mf, &root, "", "");
	ck_assert_ptr_nonnull(result);
	ck_assert_ptr_null(root);
	ck_assert_str_eq(result, "\033[1;31mfoo\033[0m\n");

	size_t result_utf8_size = 2 * strlen(result);
	char *result_utf8 = malloc(result_utf8_size);
	ck_assert_ptr_nonnull(result_utf8);
	g2u(result, strlen(result), result_utf8, result_utf8_size);
	ck_assert_str_eq(result_utf8, "\033[1;31mfoo\033[0m\n");

	free(result_utf8);
	free(result);
}

START_TEST(parse_article_js_internal_content_with_attach) {
	char *s = strdup("AUTHOR BOARD\n"
		"TITLE\n"
		"SITE\n"
		"\n"
		"foo\n"
		"beginbinaryattach 1.txt\n"
		DELIMITER
		"SIZE"
		"bar\n"
		"\n"
		"--\n"
		"FROM");
	ck_assert_ptr_nonnull(s);
	struct mmapfile mf = { .ptr = s, .size = strlen(s) };
	struct attach_link *root = NULL;

	// 更新附件
	char *dilimiter = strchr(s, 0xFF);
	dilimiter[0] = 0;
	unsigned int size_n = htonl(4 /* "bar\n" */);
	memcpy(dilimiter + 1, &size_n, sizeof(unsigned int));
	ck_assert_int_eq(dilimiter[1], 0);
	ck_assert_int_eq(dilimiter[2], 0);
	ck_assert_int_eq(dilimiter[3], 0);
	ck_assert_int_eq(dilimiter[4], 4);

	char *result = parse_article_js_internal(&mf, &root, "b", "f");
	ck_assert_ptr_nonnull(result);
	ck_assert_ptr_nonnull(root);

	ck_assert_int_eq(root->size, 4);
	ck_assert_str_eq(root->name, "1.txt");
	char link[256];
	snprintf(link, sizeof(link), "/" SMAGIC "/bbscon/1.txt?B=b&F=f&attachpos=%zu&attachname=/1.txt", dilimiter + 1 - s);
	ck_assert_str_eq(root->link, link);
	ck_assert_int_eq(root->signature[0], 'b');
	ck_assert_int_eq(root->signature[1], 'a');
	ck_assert_int_eq(root->signature[2], 'r');
	ck_assert_int_eq(root->signature[3], '\n');
	ck_assert_int_eq(root->signature[4], 0);
	ck_assert_ptr_null(root->next);

	ck_assert_str_eq(result, "foo\n#attach 1.txt\n\n");
	free(result);
	free_attach_link_list(root);
}

END_TEST

Suite *test_article_parser_js_internal(void) {
	Suite *s = suite_create("check article parse results");
	TCase *tc = tcase_create("");
	tcase_add_test(tc, parse_article_js_internal_plain_content);
	tcase_add_test(tc, parse_article_js_internal_plain_content_with_ansi);
	tcase_add_test(tc, parse_article_js_internal_content_with_attach);
	suite_add_tcase(s, tc);

	return s;
}

