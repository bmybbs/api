#include <onion/response.h>
#include <time.h>
#include <string.h>
#include <sys/file.h>
#include <json-c/json.h>
#include <hiredis/hiredis.h>
#include <onion/dict.h>

#include "bbs.h"
#include "ytht/crypt.h"
#include "ytht/strlib.h"
#include "ytht/common.h"
#include "ytht/random.h"
#include "bmy/convcode.h"
#include "bmy/user.h"
#include "bmy/2fa.h"
#include "ythtbbs/identify.h"
#include "ythtbbs/misc.h"
#include "ythtbbs/user.h"
#include "ythtbbs/override.h"
#include "ythtbbs/notification.h"
#include "ythtbbs/permissions.h"
#include "ythtbbs/session.h"
#include "bmy/cookie.h"

#include "api.h"
#include "apilib.h"

enum activation_code_query_result {
	ACQR_NOT_EXIST	= -1,	// 激活码不存在
	ACQR_NORMAL		= 0,	// 激活码正确
	ACQR_USED		= 1,	// 激活码已使用
	ACQR_DBERROR	= -2	// 数据库错误
};

/** 查询激活码是否可用
 * @param code 激活码
 * @return 查看 activation_code_query_result
 */
static int activation_code_query(const char *code);

/** 使用激活码注册用户
 *
 * @param x
 * @param code
 */
static int adduser_with_activation_code(struct userec *x, const char *code);

static void api_newcomer(struct userec *x, const char *fromhost, char *words);

/** 好友、黑名单的显示
 *
 * @param ONION_FUNC_PROTO_STR
 * @param mode
 * @return
 */
static int api_user_override_File_list(ONION_FUNC_PROTO_STR, enum ythtbbs_override_type mode);

/** 好友、黑名单的用户添加
 *
 * @param ONION_FUNC_PROTO_STR
 * @param mode
 * @return
 */
static int api_user_override_File_add(ONION_FUNC_PROTO_STR, enum ythtbbs_override_type mode);

/** 好友、黑名单的用户删除
 *
 * @param ONION_FUNC_PROTO_STR
 * @param mode
 * @return
 */
static int api_user_override_File_del(ONION_FUNC_PROTO_STR, enum ythtbbs_override_type mode);

int api_user_login(ONION_FUNC_PROTO_STR)
{
	if (!api_check_method(req, OR_POST))
		return api_error(p, req, res, API_RT_WRONGMETHOD); //只允许POST请求

	const onion_dict *param_dict = onion_request_get_query_dict(req);
	const char * userid = onion_dict_get(param_dict, "userid");
	const char * passwd = onion_dict_get(param_dict, "passwd");
	const char * fromhost = onion_request_get_header(req, "X-Real-IP");

	time_t now_t = time(NULL);
	time_t dtime;
	struct tm dt;
	int t;
	int utmp_index;

	if(userid == NULL || passwd == NULL) {
		return api_error(p, req, res, API_RT_WRONGPARAM);
	}

	if(!strcmp(userid, ""))
		userid = "guest";

	struct userec ue;
	struct user_info ui;
	int r = ythtbbs_user_login(userid, passwd, fromhost, YTHTBBS_LOGIN_API, &ui, &ue, &utmp_index);
	if(r != YTHTBBS_USER_LOGIN_OK) { // TODO: 检查是否还有未释放的资源
		return api_error(p, req, res, r);
	}

	struct json_object *obj = json_object_new_object();
	json_object_object_add(obj, "userid", json_object_new_string(ue.userid));
	json_object_object_add(obj, "sessid", json_object_new_string(ui.sessionid));

	struct bmy_cookie cookie = {
		.userid = ui.userid,
		.sessid = ui.sessionid,
		.token  = ""
	};
	char buf[60];
	bmy_cookie_gen(buf, sizeof(buf), &cookie);
	api_set_json_header(res);
	onion_response_add_cookie(res, SMAGIC, buf, -1, NULL, NULL, 0); // TODO 检查 cookie 的有效期
	onion_response_write0(res, json_object_to_json_string(obj));

	json_object_put(obj);
	return OCS_PROCESSED;
}

int api_user_query(ONION_FUNC_PROTO_STR)
{
	DEFINE_COMMON_SESSION_VARS;
	int rc;

	if (!api_check_method(req, OR_GET))
		return api_error(p, req, res, API_RT_WRONGMETHOD);

	rc = api_check_session(req, cookie_buf, sizeof(cookie_buf), &cookie, &utmp_idx, &ptr_info);
	if (rc != API_RT_SUCCESSFUL)
		return api_error(p, req, res, rc);

	const char * queryid = onion_request_get_query(req, "queryid"); //查询id
	char buf[4096];
	struct userec *ue;

	if(!queryid || queryid[0]=='\0' || strcasecmp(queryid, ptr_info->userid) == 0) {
		// 查询自己
		ue = getuser(ptr_info->userid);
		if(ue == 0)
			return api_error(p, req, res, API_RT_NOSUCHUSER);

		int unread_mail;
		mail_count(ue->userid, &unread_mail);
		sprintf(buf, "{\"errcode\":0, \"userid\":\"%s\", \"login_counts\":%d,"
				"\"post_counts\":%d, \"unread_mail\":%d, \"unread_notify\":%d,"
				"\"job\":\"%s\", \"exp\":%d, \"perf\":%d,"
				"\"exp_level\":\"%s\", \"perf_level\":\"%s\"}",
				ue->userid, ue->numlogins, ue->numposts, unread_mail,
				count_notification_num(ue->userid), getuserlevelname(ue->userlevel),
				countexp(ue), countperf(ue),
				calc_exp_str_utf8(countexp(ue)), calc_perf_str_utf8(countperf(ue)));
	} else {
		// 查询对方id
		ue = getuser(queryid);
		if(ue == 0)
			return api_error(p, req, res, API_RT_NOSUCHUSER);

		sprintf(buf, "{\"errcode\":0, \"userid\":\"%s\", \"login_counts\":%d,"
				"\"post_counts\":%d, \"job\":\"%s\", \"exp_level\":\"%s\","
				"\"perf_level\":\"%s\"}", ue->userid, ue->numlogins, ue->numposts,
				getuserlevelname(ue->userlevel),
				calc_exp_str_utf8(countexp(ue)), calc_perf_str_utf8(countperf(ue)));
	}

	struct json_object *jp = json_tokener_parse(buf);
	json_object_object_add(jp, "nickname", json_object_new_string(ue->username));

	api_set_json_header(res);
	onion_response_write0(res, json_object_to_json_string(jp));

	json_object_put(jp);
	free(ue);

	return OCS_PROCESSED;
}

int api_user_logout(ONION_FUNC_PROTO_STR)
{
	DEFINE_COMMON_SESSION_VARS;
	int rc;

	if (!api_check_method(req, OR_POST))
		return api_error(p, req, res, API_RT_WRONGMETHOD); //只允许POST请求

	rc = api_check_session(req, cookie_buf, sizeof(cookie_buf), &cookie, &utmp_idx, &ptr_info);
	if (rc != API_RT_SUCCESSFUL)
		return api_error(p, req, res, rc);

	ythtbbs_user_logout(ptr_info->userid, utmp_idx); // TODO return value

	onion_response_add_cookie(res, SMAGIC, "", 0, NULL, NULL, 0);
	return api_error(p, req, res, API_RT_SUCCESSFUL);
}

int api_user_check_session(ONION_FUNC_PROTO_STR)
{
	DEFINE_COMMON_SESSION_VARS;
	int rc = api_check_session(req, cookie_buf, sizeof(cookie_buf), &cookie, &utmp_idx, &ptr_info);
	if(rc != API_RT_SUCCESSFUL) {
		return api_error(p, req, res, rc);
	}

	api_set_json_header(res);
	onion_response_write0(res, "{\"errcode\":0}");

	return OCS_PROCESSED;
}

int api_user_register(ONION_FUNC_PROTO_STR)
{
	const char *userid = onion_request_get_query(req, "userid");
	const char *passwd = onion_request_get_query(req, "passwd");
	const char *active = onion_request_get_query(req, "activation");	// 激活码，仅供测试使用
	const char * fromhost = onion_request_get_header(req, "X-Real-IP");

	if(userid == NULL || passwd == NULL || active == NULL) {
		return api_error(p, req, res, API_RT_WRONGPARAM);
	}

	if(id_with_num(userid) || strlen(userid)<2 || strlen(passwd)<4 || strlen(active)!=9) {
		return api_error(p, req, res, API_RT_WRONGPARAM);
	}

	if(ytht_badstr(passwd) || ytht_badstr(userid)) {
		return api_error(p, req, res, API_RT_WRONGPARAM);
	}

	if(is_bad_id(userid)) {
		return api_error(p, req, res, API_RT_FBDUSERNAME);
	}

	struct userec *ue = getuser(userid);
	if(ue) {
		free(ue);
		return api_error(p, req, res, API_RT_USEREXSITED);
	}

	if(activation_code_query(active) != ACQR_NORMAL) {
		return api_error(p, req, res, API_RT_WRONGACTIVE);
	}

	struct userec x;
	memset(&x, 0, sizeof(x));
	ytht_strsncpy(x.userid, userid, 13);

	char salt[3];
	ytht_get_salt(salt);
	ytht_strsncpy(x.passwd, ytht_crypt_crypt1(passwd, salt), 14);

	x.userlevel = PERM_DEFAULT;

	time_t now_t;
	time(&now_t);
	x.firstlogin = now_t;
	x.lastlogin = now_t - 3600;
	x.userdefine = -1;
	x.flags[0] = BRDSORT_FLAG2 | PAGER_FLAG; // see src/bbs/register.c

	adduser_with_activation_code(&x, active);

	char filename[80];
	sethomepath_s(filename, sizeof(filename), userid);
	mkdir(filename, 0755);

	api_newcomer(&x, fromhost, "");

	char buf[256];
	sprintf(buf, "%s newaccount %d %s api", x.userid, getusernum(x.userid), fromhost);
	newtrace(buf);

	api_set_json_header(res);
	onion_response_write0(res, "{\"errcode\":0}");

	return OCS_PROCESSED;
}

int api_user_articlequery(ONION_FUNC_PROTO_STR)
{
	const char *userid = onion_request_get_query(req, "userid");
	const char *sessid = onion_request_get_query(req, "sessid");
	const char *appkey = onion_request_get_query(req, "appkey");
	const char *qryuid = onion_request_get_query(req, "query_user");
	const char *qryday_str = onion_request_get_query(req, "query_day");

	if(userid == NULL || sessid == NULL || appkey == NULL || qryuid == NULL)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	struct userec *ue = getuser(userid);
	if(ue == 0) {
		return api_error(p, req, res, API_RT_NOSUCHUSER);
	}

	struct userec *query_ue = getuser(qryuid);
	if(query_ue == 0) {
		// 查询的对方用户不存在
		free(ue);
		return api_error(p, req, res, API_RT_NOSUCHUSER);
	}

	int r = check_user_session(ue, sessid, appkey);
	if(r != API_RT_SUCCESSFUL) {
		free(ue);
		free(query_ue);
		return api_error(p, req, res, r);
	}

	// 通过权限检验，从 redis 中寻找缓存，若成功则使用缓存中的内容
	redisContext * rContext;
	redisReply * rReplyOut, * rReplyTime;
	rContext = redisConnect((char *)"127.0.0.1", 6379);

	time_t now_t = time(NULL);
	if(rContext!=NULL && rContext->err ==0) {
		// 连接成功的情况下才执行

		rReplyTime = redisCommand(rContext, "GET useractivities-%s-%s-timestamp",
				ue->userid, query_ue->userid);

		if(rReplyTime->str != NULL) {
			// 存在缓存
			time_t cache_time = atol(rReplyTime->str);
			freeReplyObject(rReplyTime);

			if(now_t - cache_time < 300) {
				// 缓存时间小于 5min 才使用缓存
				rReplyOut = redisCommand(rContext, "GET useractivities-%s-%s",
						ue->userid, query_ue->userid);

				// 输出
				api_set_json_header(res);
				onion_response_write0(res, rReplyOut->str);

				// 释放资源并结束
				freeReplyObject(rReplyOut);
				redisFree(rContext);
				free(query_ue);
				free(ue);

				return OCS_PROCESSED;
			}
		}
	}

	if(rContext) {
		redisFree(rContext);
	}

	const int MAX_SEARCH_NUM = 1000;
	struct api_article * articles = (struct api_article*)malloc(sizeof(struct api_article) * MAX_SEARCH_NUM);
	memset(articles, 0, sizeof(struct api_article) * MAX_SEARCH_NUM);

	int qryday = 3; // 默认为3天
	if(qryday_str != NULL && atoi(qryday_str) > 0)
		qryday = atoi(qryday_str);

	struct user_info * ui = ythtbbs_cache_utmp_get_by_idx(get_user_utmp_index(sessid));
	int num = search_user_article_with_title_keywords(articles, MAX_SEARCH_NUM, ui,
			query_ue->userid, NULL, NULL, NULL, qryday * 86400);

	// 输出
	char * tmp_buf = NULL;
	asprintf(&tmp_buf, "{\"errcode\":0, \"userid\":\"%s\", \"total\":%d, \"articles\":[]}",
			query_ue->userid, num);
	struct json_object *output_obj = json_tokener_parse(tmp_buf);
	free(tmp_buf);

	struct json_object *output_array = json_object_object_get(output_obj, "articles");
	struct json_object *json_board_obj=NULL, *json_articles_array=NULL, *json_article_obj=NULL;

	int i;
	char * curr_board = NULL;	// 判断版面名
	struct api_article *ap;
	struct boardmem *b;
	for(i=0; i<num; ++i) {
		ap = &articles[i];

		if(strcmp(curr_board, ap->board) != 0) {
			// 新的版面
			curr_board = ap->board;
			b = ythtbbs_cache_Board_get_board_by_name(curr_board);

			asprintf(&tmp_buf, "{\"board\":\"%s\", \"secstr\":\"%s\", \"articles\":[]}", curr_board, b->header.sec1);
			json_board_obj = json_tokener_parse(tmp_buf);
			free(tmp_buf);

			json_object_array_add(output_array, json_board_obj);

			json_articles_array = json_object_object_get(json_board_obj, "articles");
		}

		// 添加实体，因为此时 json_articles_array 指向了正确的数组
		asprintf(&tmp_buf, "{\"aid\":%ld, \"tid\":%ld, \"mark\":%d, \"num\":%d}",
				ap->filetime, ap->thread, ap->mark, ap->sequence_num);
		json_article_obj = json_tokener_parse(tmp_buf);
		free(tmp_buf);
		json_object_object_add(json_article_obj, "title", json_object_new_string(ap->title));

		json_object_array_add(json_articles_array, json_article_obj);
	}

	char *s = strdup(json_object_to_json_string(output_obj));

	api_set_json_header(res);
	onion_response_write0(res, s);

	// 缓存到 redis
	rContext = redisConnect((char *)"127.0.0.1", 6379);
	if(rContext!=NULL && rContext->err ==0) {
		// 连接成功的情况下才执行
		rReplyTime = redisCommand(rContext, "SET useractivities-%s-%s-timestamp %d",
				ue->userid, query_ue->userid, now_t);
		rReplyOut = redisCommand(rContext, "SET useractivities-%s-%s %s",
				ue->userid, query_ue->userid, s);

		asprintf(&tmp_buf, "[redis] SET %s and %s", rReplyTime->str, rReplyOut->str);
		newtrace(tmp_buf);

		freeReplyObject(rReplyTime);
		freeReplyObject(rReplyOut);
		free(tmp_buf);
	}

	if(rContext) {
		redisFree(rContext);
	}

	free(s);
	json_object_put(output_obj);
	free(articles);
	free(query_ue);
	free(ue);

	return OCS_PROCESSED;
}

int api_user_friends_list(ONION_FUNC_PROTO_STR)
{
	return api_user_override_File_list(p, req, res, YTHTBBS_OVERRIDE_FRIENDS);
}

int api_user_rejects_list(ONION_FUNC_PROTO_STR)
{
	return api_user_override_File_list(p, req, res, YTHTBBS_OVERRIDE_REJECTS);
}

int api_user_friends_add(ONION_FUNC_PROTO_STR)
{
	return api_user_override_File_add(p, req, res, YTHTBBS_OVERRIDE_FRIENDS);
}

int api_user_rejects_add(ONION_FUNC_PROTO_STR)
{
	return api_user_override_File_add(p, req, res, YTHTBBS_OVERRIDE_REJECTS);
}

int api_user_friends_del(ONION_FUNC_PROTO_STR)
{
	return api_user_override_File_del(p, req, res, YTHTBBS_OVERRIDE_FRIENDS);
}

int api_user_rejects_del(ONION_FUNC_PROTO_STR)
{
	return api_user_override_File_del(p, req, res, YTHTBBS_OVERRIDE_REJECTS);
}

static int autocomplete_callback(const struct ythtbbs_cache_User *user, int curr_idx, va_list ap) {
	char *search_str = va_arg(ap, char *);
	struct json_object *json_array_user = va_arg(ap, struct json_object *); // TODO

	if (strcasestr(user->userid, search_str) == user->userid)
		json_object_array_add(json_array_user, json_object_new_string(user->userid));

	return 0;
}

int api_user_autocomplete(ONION_FUNC_PROTO_STR)
{
	const char * search_str = onion_request_get_query(req, "search_str");

	if (!search_str)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	if (strlen(search_str) < 2)
		return api_error(p, req, res, API_RT_SUCCESSFUL);

	struct json_object *obj = json_tokener_parse("{\"errcode\":0, \"user_array\":[]}");
	struct json_object *json_array_user = json_object_object_get(obj, "user_array");

	ythtbbs_cache_UserTable_foreach_v(autocomplete_callback, search_str, json_array_user);

	api_set_json_header(res);
	onion_response_write0(res, json_object_to_json_string(obj));

	json_object_put(obj);

	return OCS_PROCESSED;
}

__attribute__((deprecated)) static int activation_code_query(const char *code)
{
	return ACQR_NORMAL;
}

// TODO deprecated
static int adduser_with_activation_code(struct userec *x, const char *code) {
	return ACQR_NORMAL;
}

static void api_newcomer(struct userec *x, const char *fromhost, char *words)
{
	FILE *fp;
	char filename[80];
	sprintf(filename, "bbstmpfs/tmp/%s.tmp", x->userid);
	fp = fopen(filename, "w");
	fprintf(fp, "大家好, \n\n");
	fprintf(fp, "我是 %s(%s), 来自 %s\n", x->userid, x->username, fromhost);
	fprintf(fp, "今天初来此地报到, 请大家多多指教.\n\n");
	fprintf(fp, "自我介绍:\n\n");
	fprintf(fp, "%s", words);
	fclose(fp);
	do_article_post("newcomers", "API 新手上路", filename, x->userid,
		x->username, fromhost, -1, 0, 0, x->userid, -1);
	unlink(filename);
}

static int api_user_override_File_list(ONION_FUNC_PROTO_STR, enum ythtbbs_override_type mode)
{
	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");

	if(!userid || !sessid || !appkey)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_NOSUCHUSER);

	int r = check_user_session(ue, sessid, appkey);
	if(r != API_RT_SUCCESSFUL) {
		free(ue);
		return api_error(p, req, res, r);
	}

	struct ythtbbs_override * array;
	int size=0;
	if(mode == YTHTBBS_OVERRIDE_FRIENDS) {
		array = (struct ythtbbs_override *)malloc(sizeof(struct ythtbbs_override) * MAXFRIENDS);
		size = MAXFRIENDS;
	} else {
		array = (struct ythtbbs_override *)malloc(sizeof(struct ythtbbs_override) * MAXREJECTS);
		size = MAXREJECTS;
	}
	size = ythtbbs_override_get_records(ue->userid, array, size, mode);

	char exp_utf[2*sizeof(array[0].exp)];
	struct json_object * obj = json_tokener_parse("{\"errcode\":0, \"users\":[]}");
	struct json_object * json_array_users = json_object_object_get(obj, "users");

	int i;
	for(i=0; i<size; ++i) {
		struct json_object * user = json_object_new_object();
		json_object_object_add(user, "userid", json_object_new_string(array[i].id));

		memset(exp_utf, 0, sizeof(exp_utf));
		g2u(array[i].exp, strlen(array[i].exp), exp_utf, sizeof(exp_utf));
		json_object_object_add(user, "explain", json_object_new_string(exp_utf));
		json_object_array_add(json_array_users, user);
	}

	api_set_json_header(res);
	onion_response_write0(res, json_object_to_json_string(obj));

	json_object_put(obj);
	free(array);
	free(ue);

	return OCS_PROCESSED;
}

static int api_user_override_File_add(ONION_FUNC_PROTO_STR, enum ythtbbs_override_type mode)
{
	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");
	const char * queryid = onion_request_get_query(req, "queryid");
	const char * exp_utf = onion_request_get_query(req, "explain");
	int lockfd;

	if(!userid || !sessid || !appkey || !queryid)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_NOSUCHUSER);

	struct userec *query_ue = getuser(queryid);
	if(query_ue == 0) {
		free(ue);
		return api_error(p, req, res, API_RT_NOSUCHUSER);
	}

	int r = check_user_session(ue, sessid, appkey);
	if(r != API_RT_SUCCESSFUL) {
		free(ue);
		free(query_ue);
		return api_error(p, req, res, r);
	}

	if(ythtbbs_override_included(ue->userid, mode, queryid)) {
		// queryid 已存在
		free(ue);
		free(query_ue);
		return api_error(p, req, res, API_RT_ALRDYINRCD);
	}

	size_t size;

	lockfd = ythtbbs_override_lock(ue->userid, mode);
	size = ythtbbs_override_count(ue->userid, mode);
	if (size >= (mode == YTHTBBS_OVERRIDE_FRIENDS ? MAXFRIENDS : MAXREJECTS) - 1) {
		ythtbbs_override_unlock(lockfd);
		free(ue);
		free(query_ue);
		return api_error(p, req, res, API_RT_REACHMAXRCD);
	}

	struct ythtbbs_override of;
	strcpy(of.id, query_ue->userid);
	u2g(exp_utf, strlen(exp_utf), of.exp, sizeof(of.exp));

	ythtbbs_override_add(ue->userid, &of, mode);
	ythtbbs_override_unlock(lockfd);

	api_set_json_header(res);
	onion_response_printf(res, "{ \"errcode\": 0, \"userid\": \"%s\" }", query_ue->userid);

	free(ue);
	free(query_ue);

	return OCS_PROCESSED;
}

static int api_user_override_File_del(ONION_FUNC_PROTO_STR, enum ythtbbs_override_type mode)
{
	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");
	const char * queryid = onion_request_get_query(req, "queryid");
	int lockfd;

	if(!userid || !sessid || !appkey || !queryid)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_NOSUCHUSER);

	struct userec *query_ue = getuser(queryid);
	if(query_ue == 0) {
		free(ue);
		return api_error(p, req, res, API_RT_NOSUCHUSER);
	}

	int r = check_user_session(ue, sessid, appkey);
	if(r != API_RT_SUCCESSFUL) {
		free(ue);
		free(query_ue);
		return api_error(p, req, res, r);
	}

	if(!ythtbbs_override_included(ue->userid, mode, queryid)) {
		// queryid 不存在
		free(ue);
		free(query_ue);
		return api_error(p, req, res, API_RT_NOTINRCD);
	}

	struct ythtbbs_override * array;
	int size;
	if (mode == YTHTBBS_OVERRIDE_FRIENDS) {
		array = (struct ythtbbs_override *)malloc(sizeof(struct ythtbbs_override) * MAXFRIENDS);
		size = MAXFRIENDS;
	} else {
		array = (struct ythtbbs_override *)malloc(sizeof(struct ythtbbs_override) * MAXREJECTS);
		size = MAXREJECTS;
	}

	lockfd = ythtbbs_override_lock(ue->userid, mode);
	size = ythtbbs_override_get_records(ue->userid, array, size, mode);

	int i;
	for (i = 0; i < size - 1; ++i) {
		if (strcasecmp(array[i].id, queryid) == 0) {
			array[i].id[0] = '\0';
			size--;
			break;
		}
	}

	ythtbbs_override_set_records(ue->userid, array, size, mode);
	ythtbbs_override_unlock(lockfd);

	api_set_json_header(res);
	onion_response_printf(res, "{ \"errcode\": 0, \"userid\": \"%s\" }", query_ue->userid);

	free(array);
	free(ue);
	free(query_ue);
	return OCS_PROCESSED;
}

#define BMY_2FA_KEY_SIZE (32 + 1)
static const char *SESSION_2FA_KEY = "TFAKEY";

int api_user_wx_2fa_get_key(ONION_FUNC_PROTO_STR) {
	DEFINE_COMMON_SESSION_VARS;
	int rc;
	char key[BMY_2FA_KEY_SIZE], local_buf[128];
	char *old_key = NULL;
	bmy_2fa_status status;

	if (!api_check_method(req, OR_GET))
		return api_error(p, req, res, API_RT_WRONGMETHOD);

	rc = api_check_session(req, cookie_buf, sizeof(cookie_buf), &cookie, &utmp_idx, &ptr_info);
	if (rc != API_RT_SUCCESSFUL)
		return api_error(p, req, res, rc);

	if (bmy_user_has_openid(utmp_idx + 1)) {
		return api_error(p, req, res, API_RT_HASOPENID);
	}

	old_key = ythtbbs_session_get_value(cookie.sessid, SESSION_2FA_KEY);
	if (old_key && (bmy_2fa_valid(old_key) == BMY_2FA_SUCCESS)) {
		snprintf(local_buf, sizeof(local_buf), "{\"errcode\": 0, \"key\":\"%s\"}", old_key);
		free(old_key);

		api_set_json_header(res);
		onion_response_write0(res, local_buf);
		return OCS_PROCESSED;
	}

	if (old_key)
		free(old_key);

	status = bmy_2fa_create(key, BMY_2FA_KEY_SIZE);
	if (status != BMY_2FA_SUCCESS) {
		snprintf(local_buf, sizeof(local_buf), "generate 2fa failed for %s, status: %d", ptr_info->userid, status);
		newtrace(local_buf);
		return api_error(p, req, res, API_RT_2FA_INTERNAL);
	}

	// 这里 key 的长度是明确的，且是 json 安全的字符，因此就不额外声明缓冲区了，并且直接生成 json
	snprintf(local_buf, sizeof(local_buf), "{\"errcode\": 0, \"key\":\"%s\"}", key);

	api_set_json_header(res);
	onion_response_write0(res, local_buf);

	ythtbbs_session_set_value(cookie.sessid, SESSION_2FA_KEY, key);
	return OCS_PROCESSED;
}

