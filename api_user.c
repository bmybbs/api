#include "api.h"

/**
 * @brief 依据 appkey 登录用户
 * @param ue
 * @param appkey
 * @param utmp_pos 传出参数，给出 utmp 中的索引值，用于生成 SESSION 字符串
 * @return 返回 error_code 错误码
 */
static int api_do_login(struct userec *ue, const char * appkey, int *utmp_pos);

int api_user_login(ONION_FUNC_PROTO_STR)
{
	const onion_dict *param_dict = onion_request_get_query_dict(req);
	const char * userid = onion_dict_get(param_dict, "userid");
	const char * passwd = onion_dict_get(param_dict, "passwd");
	const char * appkey = onion_dict_get(param_dict, "appkey");
	const char * fromhost = onion_request_get_client_description(req);

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

	int r = api_do_login(ue, appkey, &utmp_index);
	if(r != API_RT_SUCCESSFUL) { // TODO: 检查是否还有未释放的资源
		return api_error(p, req, res, r);
	}

	api_template_t tpl = api_template_create("templates/api_user_login.json");
	api_template_set(&tpl, "UserID", ue->userid);
	api_template_set(&tpl, "SessionID", "%c%c%c%s",
			utmp_index / 26 / 26 + 'A',
			utmp_index / 26 % 26 + 'A',
			utmp_index % 26,
			shm_utmp->uinfo[utmp_index].sessionid);
	api_template_set(&tpl, "Token", shm_utmp->uinfo[utmp_index].token);

	onion_response_set_header(res, "Content-type", "application/json; charset=utf-8");
	onion_response_printf(res, "%s", tpl);

	api_template_free(tpl);
	free(ue);
	return OCS_NOT_IMPLEMENTED;
}

int api_user_query(ONION_FUNC_PROTO_STR)
{
	const onion_dict *param_dict = onion_request_get_query_dict(req);
	const char * userid = onion_dict_get(param_dict, "userid");

	if(userid==NULL) {
		return api_error(p, req, res, API_RT_WRONGPARAM);
	}

	if(userid[0]=='\0') {
		return api_error(p, req, res, API_RT_WRONGPARAM);
	}

	struct userec *ue;

	ue = getuser(userid);
	if(ue == 0) {
		return api_error(p, req, res, API_RT_NOSUCHUSER);
	}

	api_template_t tpl = api_template_create("templates/api_user_info.json");
	if(tpl==NULL) {
		return api_error(p, req, res, API_RT_NOTEMPLATE);
	}
	api_template_set(&tpl, "UserID", "%s", ue->userid);
	api_template_set(&tpl, "UserNickName", "%s", ue->username);
	api_template_set(&tpl, "LoginCounts", "%d", ue->numlogins);
	api_template_set(&tpl, "PostCounts", "%d", ue->numposts);

	onion_response_set_header(res, "Content-type", "application/json; charset=utf-8");
	onion_response_printf(res, "%s", tpl);

	api_template_free(tpl);
	free(ue);

	return OCS_PROCESSED;
}

int api_user_logout(ONION_FUNC_PROTO_STR)
{
	return OCS_NOT_IMPLEMENTED;
}

int api_user_check_session(ONION_FUNC_PROTO_STR)
{
	return OCS_NOT_IMPLEMENTED;
}
