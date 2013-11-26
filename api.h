#ifndef __BMYBBS_API_H
#define __BMYBBS_API_H
#include "apilib.h"
#include "error_code.h"

#include <onion/onion.h>
#include <onion/dict.h>
#include <onion/log.h>
#include <onion/block.h>
#include <signal.h>
#include <netdb.h>

extern onion *o;

#define ONION_FUNC_PROTO_STR void *p, onion_request *req, onion_response *res

int api_error(ONION_FUNC_PROTO_STR, enum api_error_code errcode);

int api_user_login(ONION_FUNC_PROTO_STR);
int api_user_query(ONION_FUNC_PROTO_STR);
int api_user_logout(ONION_FUNC_PROTO_STR);
int api_user_check_session(ONION_FUNC_PROTO_STR);

int api_article_list(ONION_FUNC_PROTO_STR);

int api_article_getHTMLContent(ONION_FUNC_PROTO_STR);	// 获取 HTML 格式的内容
int api_article_getRAWContent(ONION_FUNC_PROTO_STR);	// 获取原始内容，'\033' 字符将被转为 "[ESC]" 字符串

int api_article_post(ONION_FUNC_PROTO_STR);				// 发帖接口
int api_article_reply(ONION_FUNC_PROTO_STR);			// 回帖

int api_board_list(ONION_FUNC_PROTO_STR);

/**
 * @brief 为 onion_response 添加 json 的 MIME 信息
 * @param res
 * @return
 */
static inline void api_set_json_header(onion_response *res)
{
	onion_response_set_header(res, "Content-Type", "application/json; charset=utf-8");
}

#endif
