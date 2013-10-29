#include "api.h"

struct bmy_article {
	int type;				///< 类型，1表示主题，0表示一般文章
	char title[80];			///< 标题
	char board[24];			///< 版面id
	char author[16];		///< 作者id
	int filetime;			///< fileheader.filetime，可以理解为文章id
	int thread;				///< 主题id
	int th_num;				///< 参与主题讨论的人数
	unsigned int mark; 		///< 文章标记，fileheader.accessed
};

/**
 * @brief 将 struct bmy_article 数组序列化为 json 字符串。
 * 这个方法不考虑异常，因此方法里确定了 errcode 为 0，也就是 API_RT_SUCCESSFUL，
 * 相关的异常应该在从 BMY 数据转为 bmy_article 的过程中判断、处理。
 * @param ba_list struct bmy_article 数组
 * @param count 数组长度
 * @return json 字符串
 * @warning 记得调用完成 free
 */
static char* bmy_article_array_to_json_string(struct bmy_article *ba_list, int count);

int api_article_list(ONION_FUNC_PROTO_STR)
{
	const char *type = onion_request_get_query(req, "type");

	if(strcasecmp(type, "top10")==0) { // 十大
		return OCS_NOT_IMPLEMENTED;
	} else if(strcasecmp(type, "sectop")==0) { // 分区热门
		return OCS_NOT_IMPLEMENTED;
	} else if(strcasecmp(type, "commend")==0) { // 美文推荐
		return OCS_NOT_IMPLEMENTED;
	} else if(strcasecmp(type, "announce")==0) { // 通知公告
		return OCS_NOT_IMPLEMENTED;
	} else if(strcasecmp(type, "board")==0) { // 版面文章
		return OCS_NOT_IMPLEMENTED;
	} else if(strcasecmp(type, "thread")==0) { // 同主题列表
		return OCS_NOT_IMPLEMENTED;
	} else
		return api_error(p, req, res, API_RT_WRONGPARAM);
}

static char* bmy_article_array_to_json_string(struct bmy_article *ba_list, int count)
{
	char buf[512];
	int i;
	struct bmy_article *p;
	struct json_object *jp;
	struct json_object *obj = json_tokener_parse("{\"errcode\":0, \"articlelist\":[]}");
	struct json_object *json_array = json_object_object_get(obj, "articlelist");

	for(i=0; i<count; ++i) {
		p = &(ba_list[i]);
		memset(buf, 0, 512);
		sprintf(buf, "{ \"type\":%d, \"board\":%s, \"aid\":%d, \"tid\":%d, "
				"\"title\":%s, \"author\":%s, \"th_num\":%d, \"mark\":%d }",
				p->type, p->board, p->filetime, p->thread,
				p->title, p->author, p->th_num, p->mark);
		jp = json_tokener_parse(buf);
		if(jp)
			json_object_array_add(json_array, jp);
	}

	char *r = strdup(json_object_to_json_string(obj));
	json_object_put(obj);

	return r;
}
