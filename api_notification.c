#include <string.h>
#include <json-c/json.h>
#include "ythtbbs/notification.h"

#include "api.h"
#include "apilib.h"

int api_notification_list(ONION_FUNC_PROTO_STR)
{
	const char * userid = onion_request_get_query(req, "userid");
	const char * appkey = onion_request_get_query(req, "appkey");
	const char * sessid = onion_request_get_query(req, "sessid");

	if (userid == NULL || sessid == NULL || appkey == NULL) {
		return api_error(p, req, res, API_RT_WRONGPARAM);
	}

	struct userec *ue = getuser(userid);
	if (ue == 0) {
		return api_error(p, req, res, API_RT_NOSUCHUSER);
	}

	int r = check_user_session(ue, sessid, appkey);
	if (r != API_RT_SUCCESSFUL) {
		free(ue);
		return api_error(p, req, res, r);
	}

	struct json_object *obj = json_tokener_parse("{\"errcode\": 0, \"notifications\": []}");
	struct json_object *noti_array = json_object_object_get(obj, "notifications");
	NotifyItemList allNotifyItems = parse_notification(ue->userid);
	struct json_object * item = NULL;
	struct NotifyItem * currItem;
	struct boardmem *b;
	for (currItem = (struct NotifyItem *)allNotifyItems; currItem != NULL; currItem = currItem->next) {
		item = json_object_new_object();
		json_object_object_add(item, "board", json_object_new_string(currItem->board));
		json_object_object_add(item, "noti_time", json_object_new_int64(currItem->noti_time));
		json_object_object_add(item, "from_userid", json_object_new_string(currItem->from_userid));
		json_object_object_add(item, "title", json_object_new_string(currItem->title_utf));
		json_object_object_add(item, "type", json_object_new_int(currItem->type));
		b = ythtbbs_cache_Board_get_board_by_name(currItem->board);
		json_object_object_add(item, "secstr", json_object_new_string(b->header.sec1));

		json_object_array_add(noti_array, item);
	}

	free_notification(allNotifyItems);

	api_set_json_header(res);
	onion_response_write0(res, json_object_to_json_string(obj));
	json_object_put(obj);
	free(ue);

	return OCS_PROCESSED;
}

int api_notification_del(ONION_FUNC_PROTO_STR)
{
	const char * userid = onion_request_get_query(req, "userid");
	const char * appkey = onion_request_get_query(req, "appkey");
	const char * sessid = onion_request_get_query(req, "sessid");

	if (userid == NULL || sessid == NULL || appkey == NULL) {
		return api_error(p, req, res, API_RT_WRONGPARAM);
	}

	const char * type = onion_request_get_query(req, "type");
	const char * board = onion_request_get_query(req, "board");
	const char * aid_str = onion_request_get_query(req, "aid");

	if (type == NULL && (board == NULL || aid_str == NULL)) {
		return api_error(p, req, res, API_RT_WRONGPARAM);
	}

	struct userec *ue = getuser(userid);
	if (ue == 0) {
		return api_error(p, req, res, API_RT_NOSUCHUSER);
	}

	int r = check_user_session(ue, sessid, appkey);
	if (r != API_RT_SUCCESSFUL) {
		free(ue);
		return api_error(p, req, res, r);
	}

	if ((type != NULL) && (strcasecmp(type, "delall") == 0)) {
		del_all_notification(ue->userid);
	} else {
		del_post_notification(ue->userid, board, atoi(aid_str));
	}

	free(ue);
	return api_error(p, req, res, API_RT_SUCCESSFUL);
}
