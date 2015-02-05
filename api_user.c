#include "api.h"
#include "identify.h"

#define NHASH 67
/**
 * @brief 依据 appkey 登录用户
 * @param ue
 * @param appkey
 * @param utmp_pos 传出参数，给出 utmp 中的索引值，用于生成 SESSION 字符串
 * @return 返回 error_code 错误码
 */
static int api_do_login(struct userec *ue, const char *fromhost, const char * appkey, time_t login_time, int *utmp_pos);
/**
 * @brief iphash 方法，参见 src
 * @param fromhost
 * @return
 */
static int iphash(const char *fromhost);

/**
 * @brief 从 shm_uindex 中移除记录的 utmp 索引
 * 该方法来自 nju09，使用的时候注意并发冲突。
 * @param uid
 * @param utmpent
 */
static void remove_uindex(int uid, int utmpent);

static int cmpfuid(unsigned int *a, unsigned int *b);

static int initfriends(struct user_info *u);

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
static int activation_code_query(char *code);

/** 使用激活码
 * @brief 变更激活码的状态，同时设定用户 id。
 * @param code 激活码
 * @param userid 用户 ID
 * @return SQL 影响的行数
 * @warning 可能存在并发，当前应用下不予考虑
 */
static int activation_code_set_user(char *code, char *userid);

/** 使用激活码注册用户
 *
 * @param x
 * @param code
 */
static int adduser_with_activation_code(struct userec *x, char *code);

static void api_newcomer(struct userec *x, char *fromhost, char *words);

/** 好友、黑名单的显示
 *
 * @param ONION_FUNC_PROTO_STR
 * @param mode
 * @return
 */
static int api_user_X_File_list(ONION_FUNC_PROTO_STR, int mode);

/** 好友、黑名单的用户添加
 *
 * @param ONION_FUNC_PROTO_STR
 * @param mode
 * @return
 */
static int api_user_X_File_add(ONION_FUNC_PROTO_STR, int mode);

/** 好友、黑名单的用户删除
 *
 * @param ONION_FUNC_PROTO_STR
 * @param mode
 * @return
 */
static int api_user_X_File_del(ONION_FUNC_PROTO_STR, int mode);

int api_user_login(ONION_FUNC_PROTO_STR)
{
	const onion_dict *param_dict = onion_request_get_query_dict(req);
	const char * userid = onion_dict_get(param_dict, "userid");
	const char * passwd = onion_dict_get(param_dict, "passwd");
	const char * appkey = onion_dict_get(param_dict, "appkey");
	const char * fromhost = onion_request_get_header(req, "X-Real-IP");

	char buf[512];
	time_t now_t = time(NULL);
	time_t dtime;
	struct tm dt;
	int t;
	int utmp_index;

	if(userid == NULL || passwd == NULL || appkey ==NULL) {
		return api_error(p, req, res, API_RT_WRONGPARAM);
	}

	if(!strcmp(userid, ""))
		userid = "guest";

	struct userec *ue = getuser(userid);
	if(ue == 0) {
		return api_error(p, req, res, API_RT_NOSUCHUSER);
	} else if(strcasecmp(userid, "guest")) {
		if(checkbansite(fromhost)) {
			return api_error(p, req, res, API_RT_SITEFBDIP);
		} else if(userbansite(ue->userid, fromhost)) {
			return api_error(p, req, res, API_RT_FORBIDDENIP);
		} else if(!checkpasswd(ue->passwd, passwd)) {
			logattempt(ue->userid, fromhost, "API", now_t);
			return api_error(p, req, res, API_RT_ERRORPWD);
		} else if(!(check_user_perm(ue, PERM_BASIC))) {
			return api_error(p, req, res, API_RT_FBDNUSER);
		}
	}

	int r = api_do_login(ue, fromhost, appkey, now_t, &utmp_index);
	if(r != API_RT_SUCCESSFUL) { // TODO: 检查是否还有未释放的资源
		free(ue);
		return api_error(p, req, res, r);
	}

	if(strcasecmp(userid, "guest")) {
		t = ue->lastlogin;
		ue->lastlogin = now_t;

		dtime = t - 4*3600;
		localtime_r(&dtime, &dt);
		t = dt.tm_mday;

		dtime = now_t - 4*3600;
		localtime_r(&now_t, &dt);

		if(t<dt.tm_mday && ue->numdays<800)
			ue->numdays++;
		ue->numlogins++;
		strsncpy(ue->lasthost, fromhost, 16);
		save_user_data(ue);
	}

	sprintf(buf, "%s enter %s api", ue->userid, fromhost);
	newtrace(buf);

	api_template_t tpl = api_template_create("templates/api_user_login.json");
	api_template_set(&tpl, "userid", ue->userid);
	api_template_set(&tpl, "sessid", "%c%c%c%s",
			(utmp_index-1) / 26 / 26 + 'A',
			(utmp_index-1) / 26 % 26 + 'A',
			(utmp_index-1) % 26 + 'A',
			shm_utmp->uinfo[utmp_index-1].sessionid);
	api_template_set(&tpl, "token", shm_utmp->uinfo[utmp_index-1].token);

	api_set_json_header(res);
	onion_response_write0(res, tpl);

	api_template_free(tpl);
	free(ue);
	return OCS_PROCESSED;
}

int api_user_query(ONION_FUNC_PROTO_STR)
{
	const onion_dict *param_dict = onion_request_get_query_dict(req);
	const char * userid = onion_dict_get(param_dict, "userid");	//用户id
	const char * appkey = onion_request_get_query(req, "appkey");
	const char * queryid = onion_request_get_query(req, "queryid"); //查询id
	const char * sessid = onion_request_get_query(req, "sessid");
	char buf[4096];
	struct userec *ue;

	if(!queryid || queryid[0]=='\0') {
		// 查询自己
		if(!userid || !appkey || !sessid)
			return api_error(p, req, res, API_RT_WRONGPARAM);

		ue = getuser(userid);
		if(ue == 0)
			return api_error(p, req, res, API_RT_NOSUCHUSER);
		if(check_user_session(ue, sessid, appkey) != API_RT_SUCCESSFUL) {
			free(ue);
			return api_error(p, req, res, API_RT_WRONGSESS);
		}

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
	const onion_dict *param_dict = onion_request_get_query_dict(req);
	const char * userid = onion_dict_get(param_dict, "userid");
	const char * sessid = onion_dict_get(param_dict, "sessid");
	const char * appkey = onion_dict_get(param_dict, "appkey");
	const char * fromhost = onion_request_get_header(req, "X-Real-IP");
	time_t now_t = time(NULL);
	char buf[512];

	if(userid == NULL || sessid == NULL || appkey == NULL) {
		return api_error(p, req, res, API_RT_WRONGPARAM);
	}

	if(strcasecmp(userid, "guest") == 0) {
		return api_error(p, req, res, API_RT_CNTLGOTGST);
	}

	struct userec *ue = getuser(userid);
	if(ue == 0) {
		return api_error(p, req, res, API_RT_NOSUCHUSER);
	}

	int r = check_user_session(ue, sessid, appkey);
	if(r != API_RT_SUCCESSFUL) {
		free(ue);
		return api_error(p, req, res, r);
	}

	sprintf(buf, "%s exitbbs api", ue->userid);
	newtrace(buf);
	strsncpy(ue->lasthost, fromhost, 16);
	ue->lastlogout = now_t;
	save_user_data(ue);

	int utmp_index = get_user_utmp_index(sessid);
	int uid=shm_utmp->uinfo[utmp_index].uid;
	remove_uindex(uid, utmp_index+1);
	memset(&(shm_utmp->uinfo[utmp_index]), 0, sizeof(struct user_info));

	if(check_user_perm(ue, PERM_BOARDS) && count_uindex(uid)==0)
		setbmstatus(ue, 0);

	free(ue);

	return api_error(p, req, res, API_RT_SUCCESSFUL);
	return OCS_PROCESSED;
}

int api_user_check_session(ONION_FUNC_PROTO_STR)
{
	const onion_dict *param_dict = onion_request_get_query_dict(req);
	const char * userid = onion_dict_get(param_dict, "userid");
	const char * sessid = onion_dict_get(param_dict, "sessid");
	const char * appkey = onion_dict_get(param_dict, "appkey");

	if(userid == NULL || sessid == NULL || appkey == NULL) {
		return api_error(p, req, res, API_RT_WRONGPARAM);
	}

	if(!strcmp(userid, ""))
		userid="guest";

	struct userec *ue = getuser(userid);
	if(ue == 0) {
		return api_error(p, req, res, API_RT_NOSUCHUSER);
	}

	int r=check_user_session(ue, sessid, appkey);
	if(r != API_RT_SUCCESSFUL) {
		free(ue);
		return api_error(p, req, res, r);
	}

	api_set_json_header(res);
	onion_response_write0(res, "{\"errcode\":0}");
	free(ue);

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

	if(badstr(passwd) || badstr(userid)) {
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
	strsncpy(x.userid, userid, 13);

	char salt[3];
	getsalt(salt);
	strsncpy(x.passwd, crypt1(passwd, salt), 14);

	x.userlevel = PERM_DEFAULT;

	time_t now_t;
	time(&now_t);
	x.firstlogin = now_t;
	x.lastlogin = now_t - 3600;
	x.userdefine = -1;
	x.flags[0] = CURSOR_FLAG | PAGER_FLAG;

	adduser_with_activation_code(&x, active);

	char filename[80];
	sethomepath(filename, userid);
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
	redisReply * rReplyOut, rReplyTime;
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

			if(abs(now_t - cache_time) < 300) {
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
	struct bmy_article * articles = (struct bmy_article*)malloc(sizeof(struct bmy_article) * MAX_SEARCH_NUM);
	memset(articles, 0, sizeof(struct bmy_article) * MAX_SEARCH_NUM);

	int qryday = 3; // 默认为3天
	if(qryday_str!=NULL && atoi(qryday)>0)
		qryday = atoi(qryday);

	int num = search_user_article_with_title_keywords(articles, MAX_SEARCH_NUM, ue,
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
	struct bmy_article *ap;
	struct boardmem *b;
	for(i=0; i<num; ++i) {
		ap = &articles[i];

		if(strcmp(curr_board, ap->board) != 0) {
			// 新的版面
			curr_board = ap->board;
			b = getboardbyname(curr_board);

			asprintf(&tmp_buf, "{\"board\":\"%s\", \"secstr\":\"%s\", \"articles\":[]}", curr_board, b->header.sec1);
			json_board_obj = json_tokener_parse(tmp_buf);
			free(tmp_buf);

			json_object_array_add(output_array, json_board_obj);

			json_articles_array = json_object_object_get(json_board_obj, "articles");
		}

		// 添加实体，因为此时 json_articles_array 指向了正确的数组
		asprintf(&tmp_buf, "{\"aid\":%d, \"tid\":%d, \"mark\":%d, \"num\":%d}",
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
	return api_user_X_File_list(p, req, res, UFT_FRIENDS);
}

int api_user_rejects_list(ONION_FUNC_PROTO_STR)
{
	return api_user_X_File_list(p, req, res, UFT_REJECTS);
}

int api_user_friends_add(ONION_FUNC_PROTO_STR)
{
	return api_user_X_File_add(p, req, res, UFT_FRIENDS);
}

int api_user_rejects_add(ONION_FUNC_PROTO_STR)
{
	return api_user_X_File_add(p, req, res, UFT_REJECTS);
}

int api_user_friends_del(ONION_FUNC_PROTO_STR)
{
	return api_user_X_File_del(p, req, res, UFT_FRIENDS);
}

int api_user_rejects_del(ONION_FUNC_PROTO_STR)
{
	return api_user_X_File_del(p, req, res, UFT_REJECTS);
}

static int api_do_login(struct userec *ue, const char *fromhost, const char *appkey, time_t login_time, int *utmp_pos)
{
	*utmp_pos = 0;
	time_t earlest_app_time;
	int uid, i, uent_index, earlest_pos, n, clubnum;
	int insert_pos=0;
	char ULIST[STRLEN], hostnamebuf[256], buf[256], fname[80], genbuf[256];
	struct user_info *u;
	FILE *fp_ulist, *fp_clubright;
	uid = getusernum(ue->userid) + 1;

	gethostname(hostnamebuf, 256);
	sprintf(ULIST, MY_BBS_HOME "/%s.%s", ULIST_BASE, hostnamebuf);

	fp_ulist = fopen(ULIST, "a");
	flock(fileno(fp_ulist), LOCK_EX);	// TODO: 检查每一个 return 前有没有 unlock

	/* 开始遍历已经使用的槽位，若找到，则赋予 utmp_pos，
	 * 并返回 API_RT_SUCCESSFUL。
	 *
	 * UINDEX 中 0、1 用于 term，2 用于 nju09，
	 * 3、4、5 的用于 API。
	 * TODO: BBSLIB.c:2962 ？？
	 * TODO: 增大 UINDEX 应修改此处
	 */

	for(i=3; i<6; ++i) {
		uent_index = shm_uindex->user[uid-1][i];
		if(strcasecmp(shm_utmp->uinfo[uent_index-1].userid, ue->userid) == 0
				&& shm_utmp->uinfo[uent_index-1].pid == APPPID) {
			if(strcasecmp(shm_utmp->uinfo[uent_index-1].appkey, appkey) == 0) {
				*utmp_pos = uent_index;
				// TODO: 应该更新相关信息
				flock(fileno(fp_ulist), LOCK_UN);
				fclose(fp_ulist);
				return API_RT_SUCCESSFUL;
			}
		} else {
			shm_uindex->user[uid-1][i] = 0;
			insert_pos = i;
		}
	}

	/* 判断 insert_pos，如果等于 0，说明当前槽位都在被当前用户的应用使用
	 * 从 shm_utmp 踢掉最后一个使用的，并复用该位置
	 */

	if(insert_pos == 0) {
		uent_index = shm_uindex->user[uid-1][3];
		earlest_app_time = shm_utmp->uinfo[uent_index-1].lasttime;
		earlest_pos = 3;

		for(i=4; i<6; ++i) {
			uent_index = shm_uindex->user[uid-1][i];
			if(shm_utmp->uinfo[uent_index-1].lasttime < earlest_app_time) {
				earlest_app_time = shm_utmp->uinfo[uent_index-1].lasttime;
				earlest_pos = i;
			}
		}

		// 释放 earlest_pos
		*utmp_pos = shm_uindex->user[uid-1][earlest_pos];
		errlog("API stay too long, drop %s.%d", ue->userid, earlest_pos);
		sprintf(buf, "%s.%d drop api", ue->userid, earlest_pos);
		newtrace(buf);
		shm_uindex->user[uid-1][earlest_pos] = 0;
		insert_pos = earlest_pos;
		memset(&(shm_utmp->uinfo[*utmp_pos-1]), 0, sizeof(struct user_info));
	}

	/* 此时应该 shm_uindex 的位置有了，而 shm_utmp 可能有可能没有。
	 * 若不存在 shm_utmp 的位置，则先全盘索引，
	 * 找到后再完成初始化。
	 */

	if(*utmp_pos == 0) {
		for(i=0, n=iphash(fromhost)*(MAXACTIVE/NHASH); i<MAXACTIVE; i++, n++) {
			if(n>=MAXACTIVE)
				n=0;
			u = &(shm_utmp->uinfo[n]);

			if(!u->active || !u->pid) {
				*utmp_pos = n+1;
				break;
			} else if(u->pid==APPPID
					&& (login_time - u->lasttime)>3600*24) {
				errlog("API stay too long, drop %s", u->userid);
				memset(buf, 0, sizeof(buf));
				sprintf(buf, "%s drop API", u->userid);
				newtrace(buf);
				remove_uindex(u->uid, n+1);
				memset(u, 0, sizeof(struct user_info));
				*utmp_pos = n+1;	// TODO: 检查是否应该 +1
				break;
			}
		}
		if(*utmp_pos == 0) {
			flock(fileno(fp_ulist), LOCK_UN);
			fclose(fp_ulist);
			return API_RT_NOEMTSESS;
		}
	}

	/* 现在应该 shm_utmp 的位置也找出来了，同时该位置已经清空数据了，
	 * 初始化，然后再加到 shm_uindex 里面去
	 */

	u=&(shm_utmp->uinfo[*utmp_pos-1]);
	u->active = 1;
	u->uid = uid;
	u->pid = APPPID;
	u->mode = LOGIN;

	u->userlevel = ue->userlevel;
	u->lasttime = login_time;
	u->curboard = 0;

	if(check_user_perm(ue, PERM_LOGINCLOAK) &&
			(ue->flags[0] & CLOAK_FLAG))
		u->invisible = YEA;

	u->pager = 0;

	strsncpy(u->from, fromhost, 24);
	strsncpy(u->username, ue->username, NAMELEN);
	strsncpy(u->userid, ue->userid, IDLEN+1);
	strsncpy(u->appkey, appkey, APPKEYLENGTH);
	getrandomstr(u->sessionid);
	getrandomstr_r(u->token, TOKENLENGTH+1);

	if(strcasecmp(ue->userid, "guest"))
		initfriends(u);
	else
		memset(u->friend, 0, sizeof(u->friend));

	if(strcasecmp(ue->userid, "guest")) {
		sethomefile(fname, ue->userid, "clubrights");
		if((fp_clubright = fopen(fname, "r")) == NULL) {
			memset(u->clubrights, 0, 4*sizeof(int));
		} else {
			while (fgets(genbuf, STRLEN, fp_clubright) != NULL) {
				clubnum = atoi(genbuf);
				u->clubrights[clubnum/32] |= (1<<clubnum%32);
			}
			fclose(fp_clubright);
		}
	} else {
		memset(u->clubrights, 0, 4*sizeof(int));
	}

	shm_uindex->user[uid-1][insert_pos] = *utmp_pos;
	flock(fileno(fp_ulist), LOCK_UN);
	fclose(fp_ulist);

	if(check_user_perm(ue, PERM_BOARDS))
		setbmstatus(ue, 1);

	return API_RT_SUCCESSFUL;
}

static int iphash(const char *fromhost)
{
	struct in_addr addr;
	inet_aton(fromhost, &addr);
	return addr.s_addr % NHASH;
}

static void remove_uindex(int uid, int utmpent)
{
	int i;
	if (uid<=0 || uid > MAXUSERS)
		return;

	for(i=0; i<6; ++i) {
		if(shm_uindex->user[uid-1][i] == utmpent) {
			shm_uindex->user[uid-1][i] = 0;
			return;
		}
	}
}

static int initfriends(struct user_info *u)
{
	int i, fnum=0;
	char buf[128];
	FILE *fp;
	memset(u->friend, 0, sizeof(u->friend));
	sethomefile(buf, u->userid, "friends");
	u->fnum = file_size(buf) / sizeof(struct override);
	if(u->fnum <=0)
		return 0;

	u->fnum = (u->fnum>=MAXFRIENDS) ? MAXFRIENDS : u->fnum;

	struct override *fff = (struct override *)malloc(MAXFRIENDS * sizeof(struct override));
	// TODO: 判断 malloc 调用失败
	memset(fff, 0, MAXFRIENDS*sizeof(struct override));
	fp = fopen(buf, "r");
	fread(fff, sizeof(struct override), MAXFRIENDS, fp);

	for(i=0; i<u->fnum; ++i) {
		u->friend[i] = getusernum(fff[i].id) + 1;
		if(u->friend[i])
			fnum++;
		else
			fff[i].id[0]=0;
	}

	qsort(u->friend, u->fnum, sizeof(u->friend[0]), (void *)cmpfuid);
	if(fnum != u->fnum) {
		fseek(fp, 0, SEEK_SET);
		for(i=0; i<u->fnum; ++i)
			if(fff[i].id[0])
				fwrite(&(fff[i]), sizeof(struct override), 1, fp);
	}

	u->fnum = fnum;
	free(fff);
	fclose(fp);
	return fnum;

}

static int cmpfuid(unsigned int *a, unsigned int *b)
{
	return *a - *b;
}

static int activation_code_query(char *code)
{
	MYSQL *s = NULL;
	MYSQL_RES *res;
	MYSQL_ROW *row;
	int count;

	s = mysql_init(s);
	if(!my_connect_mysql(s)) {
		return ACQR_DBERROR;
	}

	char code_s[10];		// 安全的9位激活码，外加结尾的 '\0'
	snprintf(code_s, 10, "%s", code);
	char sql[80];
	sprintf(sql, "SELECT * FROM activation where code='%s';", code_s);
	mysql_real_query(s, sql, strlen(sql));
	res = mysql_store_result(s);
	row = mysql_fetch_row(res);
	count = mysql_num_rows(res);

	if(count<1) {
		mysql_close(s);
		return ACQR_NOT_EXIST;
	}

	int is_used = atoi(row[2]);
	if(is_used) {
		mysql_close(s);
		return ACQR_USED;
	}

	mysql_close(s);
	return ACQR_NORMAL;
}

static int activation_code_set_user(char *code, char *userid)
{
	MYSQL *s=NULL;
	int count;

	s = mysql_init(s);
	if(!my_connect_mysql(s)) {
		return ACQR_DBERROR;
	}

	char code_s[10];
	snprintf(code_s, 10, "%s", code);
	char sql[160];
	sprintf(sql, "UPDATE activation SET is_used=1, userid='%s' WHERE code='%s'",
			userid, code_s);
	mysql_real_query(s, sql, strlen(sql));
	count = mysql_affected_rows(s);

	mysql_close(s);
	return count;
}

static int adduser_with_activation_code(struct userec *x, char *code)
{
	int i, rt;
	int fd = open(PASSFILE ".lock", O_RDONLY | O_CREAT, 0600);
	flock(fd, LOCK_EX);

	rt = activation_code_query(code);
	if(rt!=ACQR_NORMAL) {
		flock(fd, LOCK_UN);
		close(fd);
		return rt;
	}

	for(i=0; i<MAXUSERS; i++) {
		if(shm_ucache->userid[i][0]==0) {
			if((i+1) > shm_ucache->number)
				shm_ucache->number = i+1;
			strncpy(shm_ucache->userid[i], x->userid, 13);
			insertuseridhash(shm_uidhash->uhi, UCACHE_HASH_SIZE, x->userid, i+1);
			save_user_data(x);
			break;
		}
	}

	rt = activation_code_set_user(code, x->userid);
	flock(fd, LOCK_UN);
	close(fd);
	return (rt == 1) ? ACQR_NORMAL : ACQR_DBERROR;
}

static void api_newcomer(struct userec *x,char *fromhost, char *words)
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

static int api_user_X_File_list(ONION_FUNC_PROTO_STR, int mode)
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

	struct override * array;
	int size=0;
	if(mode == UFT_FRIENDS) {
		array = (struct override *)malloc(sizeof(struct override) * MAXFRIENDS);
		size = load_user_X_File(array, MAXFRIENDS, ue->userid, UFT_FRIENDS);
	} else {
		array = (struct override *)malloc(sizeof(struct override) * MAXREJECTS);
		size = load_user_X_File(array, MAXREJECTS, ue->userid, UFT_REJECTS);
	}

	char exp_utf[2*sizeof(array[0].exp)];
	struct json_object * obj = json_tokener_parse("{\"errcode\":0, \"users\":[], \"explains\":[]}");
	struct json_object * json_array_users = json_object_object_get(obj, "users");
	struct json_object * json_array_exps = json_object_object_get(obj, "explains");
	int i;
	for(i=0; i<size; ++i) {
		json_object_array_add(json_array_users, json_object_new_string(array[i].id));
		memset(exp_utf, 0, sizeof(exp_utf));
		g2u(array[i].exp, strlen(array[i].exp), exp_utf, sizeof(exp_utf));
		json_object_array_add(json_array_exps, json_object_new_string(exp_utf));
	}

	api_set_json_header(res);
	onion_response_write0(res, json_object_to_json_string(obj));

	json_object_put(obj);
	free(array);
	free(ue);

	return OCS_PROCESSED;
}

static int api_user_X_File_add(ONION_FUNC_PROTO_STR, int mode)
{
	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");
	const char * queryid = onion_request_get_query(req, "queryid");
	const char * exp_utf = onion_request_get_query(req, "explain");

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

	struct override * array;
	int size=0;
	if(mode == UFT_FRIENDS) {
		array = (struct override *)malloc(sizeof(struct override) * MAXFRIENDS);
		size = load_user_X_File(array, MAXFRIENDS, ue->userid, UFT_FRIENDS);

		if(size >= MAXFRIENDS-1) {
			free(array);
			free(ue);
			free(query_ue);
			return api_error(p, req, res, API_RT_REACHMAXRCD);
		}
	} else {
		array = (struct override *)malloc(sizeof(struct override) * MAXREJECTS);
		size = load_user_X_File(array, MAXREJECTS, ue->userid, UFT_REJECTS);

		if(size >= MAXREJECTS-1) {
			free(array);
			free(ue);
			free(query_ue);
			return api_error(p, req, res, API_RT_REACHMAXRCD);
		}
	}

	int pos = is_queryid_in_user_X_File(queryid, array, size);
	if(pos>=0) {
		// queryid 已存在
		free(array);
		free(ue);
		free(query_ue);
		return api_error(p, req, res, API_RT_ALRDYINRCD);
	}

	strcpy(array[size].id, ue->userid);
	u2g(exp_utf, strlen(exp_utf), array[size].exp, sizeof(array[size].exp));
	size++;

	char path[256];
	if(mode == UFT_FRIENDS)
		sethomefile(path, "friends");
	else
		sethomefile(path, "rejects");
	FILE *fp = fopen(path, "w");
	if(fp) {
		flockfile(fp);
		fwrite(array, sizeof(struct override), size, fp);
		funlockfile(fp);
		fclose(fp);

		free(array);
		free(ue);
		free(query_ue);
		return api_error(p, req, res, API_RT_SUCCESSFUL);
	} else {
		free(array);
		free(ue);
		free(query_ue);
		return api_error(p, req, res, API_RT_NOSUCHFILE);
	}
}

static int api_user_X_File_del(ONION_FUNC_PROTO_STR, int mode)
{
	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");
	const char * queryid = onion_request_get_query(req, "queryid");

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

	struct override * array;
	int size=0;
	if(mode == UFT_FRIENDS) {
		array = (struct override *)malloc(sizeof(struct override) * MAXFRIENDS);
		size = load_user_X_File(array, MAXFRIENDS, ue->userid, UFT_FRIENDS);
	} else {
		array = (struct override *)malloc(sizeof(struct override) * MAXREJECTS);
		size = load_user_X_File(array, MAXREJECTS, ue->userid, UFT_REJECTS);
	}

	int pos = is_queryid_in_user_X_File(queryid, array, size);
	if(pos < 0) {
		// queryid 不存在
		free(array);
		free(ue);
		free(query_ue);
		return api_error(p, req, res, API_RT_NOTINRCD);
	}

	int i;
	for(i=pos; i<size-1; ++i) {
		memcpy(&array[i], &array[i+1], sizeof(struct override));
	}
	size--;

	char path[256];
	if(mode == UFT_FRIENDS)
		sethomefile(path, "friends");
	else
		sethomefile(path, "rejects");
	FILE *fp = fopen(path, "w");
	if(fp) {
		flockfile(fp);
		fwrite(array, sizeof(struct override), size, fp);
		funlockfile(fp);
		fclose(fp);

		free(array);
		free(ue);
		free(query_ue);
		return api_error(p, req, res, API_RT_SUCCESSFUL);
	} else {
		free(array);
		free(ue);
		free(query_ue);
		return api_error(p, req, res, API_RT_NOSUCHFILE);
	}
}
