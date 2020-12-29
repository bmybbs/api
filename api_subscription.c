#include <string.h>
#include <time.h>
#include <json-c/json.h>
#include "config.h"
#include "ytht/strlib.h"
#include "bmy/cookie.h"
#include "bmy/article.h"
#include "ythtbbs/session.h"
#include "ythtbbs/cache.h"
#include "api.h"
#include "apilib.h"
#include "error_code.h"

static const int COUNT_PER_PAGE = 40;

int api_subscription_list(ONION_FUNC_PROTO_STR) {
	DEFINE_COMMON_SESSION_VARS;
	int rc;
	char output[1024];

	if (!api_check_method(req, OR_GET))
		return api_error(p, req, res, API_RT_WRONGMETHOD);

	rc = api_check_session(req, cookie_buf, sizeof(cookie_buf), &cookie, &utmp_idx, &ptr_info);
	if (rc != API_RT_SUCCESSFUL)
		return api_error(p, req, res, rc);

	time_t now_t = time(NULL);

	const char *start_str = onion_request_get_query(req, "start");
	time_t start_time;
	if (start_str == NULL || start_str[0] == '\0')
		start_time = now_t;
	else
		start_time = atol(start_str); // TODO

	struct bmy_articles *articles = bmy_article_list_subscription_by_time(ptr_info->userid, COUNT_PER_PAGE, start_time);

	if (articles == NULL || articles->count == 0) {
		bmy_article_list_free(articles);

		api_set_json_header(res);
		onion_response_write0(res, "{ \"error_code\": 0}"); // TODO
		return OCS_PROCESSED;
	}

	struct json_object *obj = json_object_new_object();
	struct json_object *article_array = json_object_new_array_ext(articles->count);
	struct json_object *article_obj;

	size_t i;
	struct fileheader_utf *ptr_header;
	for (i = 0; i < articles->count; i++) {
		ptr_header = &articles->articles[i];
		article_obj = apilib_convert_fileheader_utf_to_jsonobj(ptr_header);
		json_object_array_put_idx(article_array, i, article_obj);
	}

	json_object_object_add(obj, "articles", article_array);

	api_set_json_header(res);
	onion_response_write0(res, json_object_to_json_string_ext(obj, JSON_C_TO_STRING_NOSLASHESCAPE));
	json_object_put(obj);
	bmy_article_list_free(articles);
	return OCS_PROCESSED;
}

