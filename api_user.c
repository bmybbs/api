#include "api.h"

int api_user_login(ONION_FUNC_PROTO_STR)
{
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
