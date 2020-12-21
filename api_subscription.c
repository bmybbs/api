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
#include "error_code.h"

static const int COUNT_PER_PAGE = 40;

int api_subscription_list(ONION_FUNC_PROTO_STR) {
	char output[1024];
	if (OR_GET != (onion_request_get_flags(req) & OR_METHODS))
		return api_error(p, req, res, API_RT_WRONGMETHOD);

	const char *cookie_str = onion_request_get_cookie(req, SMAGIC);

	if (cookie_str == NULL || cookie_str[0] == '\0')
		return api_error(p, req, res, API_RT_WRONGPARAM);

	time_t now_t = time(NULL);

	struct bmy_cookie cookie;
	char buf[512];
	ytht_strsncpy(buf, cookie_str, sizeof(buf));
	bmy_cookie_parse(buf, &cookie);

	if (cookie.userid == NULL || cookie.sessid == NULL)
		return api_error(p, req, res, API_RT_WRONGPARAM);
	if (strcasecmp(cookie.userid, "guest") == 0)
		return api_error(p, req, res, API_RT_NOTLOGGEDIN);

	int utmp_idx = ythtbbs_session_get_utmp_idx(cookie.sessid, cookie.userid);
	if (utmp_idx < 0)
		return api_error(p, req, res, API_RT_NOTLOGGEDIN);

	struct user_info *ptr_info = ythtbbs_cache_utmp_get_by_idx(utmp_idx);
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
		article_obj = json_object_new_object();

		json_object_object_add(article_obj, "boardname_en", json_object_new_string(ptr_header->boardname_en));
		json_object_object_add(article_obj, "boardname_zh", json_object_new_string(ptr_header->boardname_zh));
		json_object_object_add(article_obj, "author", json_object_new_string(ptr_header->owner));
		json_object_object_add(article_obj, "title", json_object_new_string(ptr_header->title));
		json_object_object_add(article_obj, "tid", json_object_new_int64(ptr_header->thread));
		json_object_object_add(article_obj, "count", json_object_new_int(ptr_header->count));
		json_object_object_add(article_obj, "accessed", json_object_new_int(ptr_header->accessed));

		json_object_array_put_idx(article_array, i, article_obj);
	}

	json_object_object_add(obj, "articles", article_array);

	api_set_json_header(res);
	onion_response_write0(res, json_object_to_json_string_ext(obj, JSON_C_TO_STRING_NOSLASHESCAPE));
	json_object_put(obj);
	bmy_article_list_free(articles);
	return OCS_PROCESSED;
}

