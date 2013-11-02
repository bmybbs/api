#include "api.h"

/**
 * @brief 将 boardmem 数组输出为 json 字符串
 * @warning 注意使用完成释放
 * @warning 输出的版主id仅为大版主
 * @param board_array 指针数组
 * @param count board_array 数组的长度
 * @param sortmode 排序方式，1为按英文名称，2为人气，3为在线人数。默认值为2
 * @param ui 当前会话的 user_info 指针，用于判断版面是否存在未读信息
 * @return 字符指针
 */
static char* bmy_board_array_to_json_string(struct boardmem **board_array, int count, int sortmode, const char *fromhost, struct user_info *ui);

/**
 * @brief 返回用户的收藏版面列表
 * @warning 需要用户已登录
 * @param ONION_FUNC_PROTO_STR
 * @return
 */
static int api_board_list_fav(ONION_FUNC_PROTO_STR);
static int api_board_list_sec(ONION_FUNC_PROTO_STR);

/**
 * @brief 读取收藏夹文件。
 * 该方法来自 nju09/bbsmybrd.c。
 * @param mybrd char[GOOD_BRC_NUM][STRLEN] 类型，使用前需要预先分配存储空间。
 * @param mybrdnum 收藏夹版面计数，需要预先分配存储空间。
 * @param userid
 * @return 成功返回 0.
 */
static int readmybrd(char mybrd[GOOD_BRC_NUM][80], int *mybrdnum, const char *userid);

/**
 * @brief 检查是否位于收藏夹中
 * @param board 版面名称
 * @param mybrd char[GOOD_BRC_NUM][STRLEN] 类型
 * @param mybrdnum 收藏夹版面计数
 * @return 存在返回1，否则返回0。
 */
static int ismybrd(char *board, char (*mybrd)[80], int mybrdnum);

/**
 * @brief
 * @param board
 * @param lastpost
 * @param ui
 * @return
 */
static int board_read(char *board, int lastpost, const char *fromhost, struct user_info *ui);

/**
 * @brief 比较两个版面的名称，用于 qsort 排序。
 * @param b1
 * @param b2
 * @return
 */
static int cmpboard(struct boardmem **b1, struct boardmem **b2);

/**
 * @brief 比较两个版面的人气，用于 qsort 排序。
 * @param b1
 * @param b2
 * @return
 */
static int cmpboardscore(struct boardmem **b1, struct boardmem **b2);

/**
 * @brief 比较两个版面的在线人数，用于 qsort 排序。
 * @param b1
 * @param b2
 * @return
 */
static int cmpboardinboard(struct boardmem **b1, struct boardmem **b2);

int api_board_list(ONION_FUNC_PROTO_STR)
{
	const char * secstr = onion_request_get_query(req, "secstr");
	if(!secstr)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	if(strcasecmp(secstr, "fav")==0)
		return api_board_list_fav(p, req, res);
	else
		return api_board_list_sec(p, req, res);
}

static int api_board_list_fav(ONION_FUNC_PROTO_STR)
{
	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");
	const char * sortmode_s = onion_request_get_query(req, "sortmode");
	const char * fromhost = onion_request_get_client_description(req);

	if(!userid || !sessid || !appkey)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	int sortmode = (sortmode_s) ? atoi(sortmode_s) : 2;

	if(strcasecmp(userid, "guest")==0)
		return api_error(p, req, res, API_RT_NOTLOGGEDIN);

	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_NOSUCHUSER);

	int r = check_user_session(ue, sessid, appkey);
	if(r != API_RT_SUCCESSFUL) {
		free(ue);
		return api_error(p, req, res, r);
	}

	char mybrd[GOOD_BRC_NUM][STRLEN];
	int mybrdnum;
	r = readmybrd(mybrd, &mybrdnum, ue->userid);
	if(r != API_RT_SUCCESSFUL) {
		free(ue);
		return api_error(p, req, res, r);
	}

	int i,count=0;
	struct boardmem *board_array[MAXBOARD], *p_brdmem;
	int uent_index = get_user_utmp_index(sessid);
	struct user_info *ui = &(shm_utmp->uinfo[uent_index]);
	for(i=0; i<MAXBOARD && i<shm_bcache->number; ++i) {
		p_brdmem = &(shm_bcache->bcache[i]);
		if(p_brdmem->header.filename[0]<=32 || p_brdmem->header.filename[0]>'z')
			continue;
		if(!check_user_read_perm_x(ui, p_brdmem))
			continue;
		if(!ismybrd(p_brdmem->header.filename, mybrd, mybrdnum))
			continue;
		board_array[count] = p_brdmem;
		count++;
	}

	char *s = bmy_board_array_to_json_string(board_array, count, sortmode, fromhost, ui);
	onion_response_set_header(res, "Content-type", "application/json; charset=utf-8");
	onion_response_write0(res, s);
	free(ue);
	free(s);
	return OCS_PROCESSED;
}

static int api_board_list_sec(ONION_FUNC_PROTO_STR)
{
	return OCS_NOT_IMPLEMENTED;
}

static char* bmy_board_array_to_json_string(struct boardmem **board_array, int count, int sortmode, const char *fromhost, struct user_info *ui)
{
	char buf[512];
	int i, j;
	struct boardmem *bp;
	struct json_object *jp;
	struct json_object *obj = json_tokener_parse("{\"errcode\":0, \"boardlist\":[]}");
	struct json_object *json_array = json_object_object_get(obj, "boardlist");

	if(sortmode<=0 || sortmode>3)
		sortmode = 2;
	switch (sortmode) {
	case 1:
		qsort(board_array, count, sizeof(struct boardmem *), (void *)cmpboard);
		break;
	case 2:
		qsort(board_array, count, sizeof(struct boardmem *), (void *)cmpboardscore);
		break;
	case 3:
		qsort(board_array, count, sizeof(struct boardmem *), (void *)cmpboardinboard);
		break;
	default:
		break;
	}

	for(i=0; i<count; ++i) {
		bp = board_array[i];
		memset(buf, 0, 512);

		// @warning: by IronBlood
		// 此处将 boardmem 中的部分字符字段转为 utf-8 编码，若 boardmem 发生变更
		// 相关变量长度也应依据需要处理。
		char zh_name[80], type[16], keyword[128];
		g2u(bp->header.title, 24, zh_name, 80);
		g2u(bp->header.keyword, 64, keyword, 128);
		g2u(bp->header.type, 5, type, 16);
		sprintf(buf, "{\"name\":\"%s\", \"zh_name\":\"%s\", \"type\":\"%s\", \"bm\":[],"
				"\"unread\":%d, \"voting\":%d, \"article_num\":%d, \"score\":%d,"
				"\"inboard_num\":%d, \"secstr\":\"%s\", \"keyword\":\"%s\" }",
				bp->header.filename, zh_name, type,
				board_read(bp->header.filename, bp->lastpost, fromhost, ui),
				(bp->header.flag & VOTE_FLAG),
				bp->total, bp->score,
				bp->inboard, bp->header.sec1, keyword);
		jp = json_tokener_parse(buf);
		if(jp) {
			struct json_object *bm_json_array = json_object_object_get(jp, "bm");
			for(j=0; j<4; j++) {
				if(bp->header.bm[j][0]==0)
					break;
				json_object_array_add(bm_json_array,
						json_object_new_string(bp->header.bm[j]));
			}
			json_object_array_add(json_array, jp);
		}
	}

	char *r = strdup(json_object_to_json_string(obj));
	json_object_put(obj);

	return r;
}

static int readmybrd(char mybrd[GOOD_BRC_NUM][80], int *mybrdnum, const char *userid)
{
	char file[256];
	FILE *fp;
	int len;
	*mybrdnum = 0;
	sethomefile(file, userid, ".goodbrd");
	fp = fopen(file, "r");
	if(fp) {
		while(fgets(mybrd[*mybrdnum], sizeof(mybrd[0]), fp) != NULL) {
			len = strlen(mybrd[*mybrdnum]);
			if(mybrd[*mybrdnum][len-1] == '\n')
				mybrd[*mybrdnum][len-1] = 0;
			(*mybrdnum)++;
			if(*mybrdnum >= GOOD_BRC_NUM)
				break;
		}
		fclose(fp);
		return API_RT_SUCCESSFUL;
	} else
		return API_RT_NOGDBRDFILE;
}

static int ismybrd(char *board, char (*mybrd)[80], int mybrdnum)
{
	int i;

	for(i=0; i<mybrdnum; ++i)
		if(!strcasecmp(board, mybrd[i]))
			return 1;

	return 0;
}

static int board_read(char *board, int lastpost, const char *fromhost, struct user_info *ui)
{
	struct allbrc allbrc;
	char allbrcuser[STRLEN];
	struct onebrc *pbrc, brc;

	memset(&allbrc, 0, sizeof(allbrc));
	memset(allbrcuser, 0, STRLEN);
	memset(&brc, 0, sizeof(brc));

	brc_initial(NULL, board, &allbrc, allbrcuser, fromhost, ui, &pbrc, &brc);
	return !brc_unreadt(pbrc, lastpost);
}

static int cmpboard(struct boardmem **b1, struct boardmem **b2)
{
	return strcasecmp((*b1)->header.filename, (*b2)->header.filename);
}

static int cmpboardscore(struct boardmem **b1, struct boardmem **b2)
{
	return ((*b1)->score - (*b2)->score);
}

static int cmpboardinboard(struct boardmem **b1, struct boardmem **b2)
{
	return ((*b1)->inboard - (*b2)->inboard);
}
