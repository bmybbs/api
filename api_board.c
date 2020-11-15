#include <string.h>
#include <sys/mman.h>
#include <libxml/HTMLtree.h>
#include <libxml/xpath.h>
#include <json-c/json.h>

#include "bbs.h"
#include "api.h"
#include "apilib.h"
#include "api_brc.h"

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

/**
 * @brief 返回分区版面列表
 * @param ONION_FUNC_PROTO_STR
 * @return
 */
static int api_board_list_sec(ONION_FUNC_PROTO_STR);

/**
 * @brief 返回分区版面列表（guest权限）
 * @param ONION_FUNC_PROTO_STR
 * @return
 */
static int api_board_list_sec_guest(ONION_FUNC_PROTO_STR);

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
 * @brief 检查版面是否已读
 * @param board 版面名称
 * @param lastpost 版面最后一篇帖子的filetime
 * @param ui 用户会话
 * @return 若lastpost已读，返回1，否则返回0.
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

int api_board_info(ONION_FUNC_PROTO_STR)
{
	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");
	const char * bname  = onion_request_get_query(req, "bname");

	if(!userid || !sessid || !appkey || !bname)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	struct boardmem *bmem = getboardbyname(bname);
	if(!bmem)
		return api_error(p, req, res, API_RT_NOSUCHBRD);

	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_NOSUCHUSER);

	int r = check_user_session(ue, sessid, appkey);
	if(r != API_RT_SUCCESSFUL) {
		free(ue);
		return api_error(p, req, res, r);
	}

	free(ue);

	int uent_index = get_user_utmp_index(sessid);
	struct user_info *ui = ythtbbs_cache_utmp_get_by_idx(uent_index);

	if(!check_user_read_perm_x(ui, bmem))
		return api_error(p, req, res, API_RT_NOBRDRPERM);

	char buf[512];
	char zh_name[80];//, type[16], keyword[128];
	g2u(bmem->header.title, 24, zh_name, 80);
	//g2u(bmem->header.keyword, 64, keyword, 128);
	//g2u(bmem->header.type, 5, type, 16);

	int today_num=0, thread_num=0, i;
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	time_t now_t = time(NULL);
	gmtime_r(&now_t, &tm);
	time_t day_begin = get_time_of_the_biginning_of_the_day(&tm);
	char filename[256];

	sprintf(filename, "boards/%s/.DIR", bmem->header.filename);
	int fsize = file_size_s(filename);
	int fd = open(filename, O_RDONLY);
	if(fd==0 || fsize==0)
		today_num = 0;
	else {
		struct fileheader *data = (struct fileheader*)mmap(NULL, fsize, PROT_READ, MAP_SHARED, fd, 0);
		close(fd);
		if(data == MAP_FAILED)
			today_num = 0;
		else {
			for(i=fsize/sizeof(struct fileheader)-1; data[i].filetime>day_begin; i--) {
				if(i<=0)
					break;

				today_num++;
			}

			for(i=0; i<fsize/sizeof(struct fileheader); ++i) {
				if(data[i].filetime == data[i].thread)
					++thread_num;
			}

			munmap(data, fsize);
		}
	}

	memset(filename, 0, 256);
	sethomefile(filename, ui->userid, ".goodbrd");
	sprintf(buf, "{\"errcode\":0, \"bm\":[], \"hot_topic\":[], \"is_fav\":%d,"
			"\"voting\":%d, \"article_num\":%d, \"thread_num\":%d, \"score\":%d,"
			"\"inboard_num\":%d, \"secstr\":\"%s\", \"today_new\":%d}",
			seek_in_file(filename, bmem->header.filename),
			(bmem->header.flag & VOTE_FLAG),
			bmem->total, thread_num, bmem->score,
			bmem->inboard, bmem->header.sec1, today_num);
	struct json_object *jp = json_tokener_parse(buf);

	json_object_object_add(jp, "name", json_object_new_string(bmem->header.filename));
	json_object_object_add(jp, "zh_name", json_object_new_string(zh_name));

	struct json_object *json_bm_array = json_object_object_get(jp, "bm");
	for(i=0; i<BMNUM; ++i) {
		if(bmem->header.bm[i][0] == 0)
			json_object_array_add(json_bm_array, (struct json_object*)NULL);
		else
			json_object_array_add(json_bm_array, json_object_new_string(bmem->header.bm[i]));
	}

	struct json_object *json_hot_topic_array = json_object_object_get(jp, "hot_topic");
	memset(filename, 0, 256);
	sprintf(filename, "boards/%s/TOPN", bmem->header.filename);

	htmlDocPtr doc = htmlParseFile(filename, "GBK");
	if(doc != NULL) {
		xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
		xmlXPathObjectPtr r_link, r_num;
		r_link = xmlXPathEvalExpression((const xmlChar*)"//tr/td[2]/a", ctx);
		r_num  = xmlXPathEvalExpression((const xmlChar*)"//tr/td[3]", ctx);

		if(r_link->nodesetval && r_num->nodesetval
				&& r_link->nodesetval->nodeNr==r_num->nodesetval->nodeNr) {
			for(i=0; i<r_link->nodesetval->nodeNr; ++i) {
				char *hot_title, *hot_tid_str, *hot_num_str;

				hot_title = (char *)xmlNodeGetContent(r_link->nodesetval->nodeTab[i]);
				hot_tid_str = strstr((char *)xmlGetProp(r_link->nodesetval->nodeTab[i], (const xmlChar*)"href"), "th=") + 3;
				hot_num_str = strstr((char *)xmlNodeGetContent(r_num->nodesetval->nodeTab[i]), ":") + 1;

				memset(buf, 0, 512);
				sprintf(buf, "{\"tid\":%d, \"num\":%d}", atoi(hot_tid_str), atoi(hot_num_str));
				struct json_object *hot_item = json_tokener_parse(buf);
				json_object_object_add(hot_item, "title", json_object_new_string(hot_title));
				json_object_array_add(json_hot_topic_array, hot_item);
			}
		}

		xmlXPathFreeObject(r_link);
		xmlXPathFreeObject(r_num);
		xmlXPathFreeContext(ctx);
		xmlFreeDoc(doc);
	}

	api_set_json_header(res);
	onion_response_write0(res, json_object_to_json_string(jp));
	json_object_put(jp);
	return OCS_PROCESSED;
}

int api_board_fav_add(ONION_FUNC_PROTO_STR)
{
	const char *board = onion_request_get_query(req, "board");
	const char *userid = onion_request_get_query(req, "userid");
	const char *appkey = onion_request_get_query(req, "appkey");
	const char *sessid = onion_request_get_query(req, "sessid");

	if(!board || !userid || !appkey || !sessid)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	if(strcasecmp(userid, "guest")==0)
		return api_error(p, req, res, API_RT_NOTLOGGEDIN);

	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_NOSUCHUSER);

	int r = check_user_session(ue, sessid, appkey);
	free(ue);
	if(r != API_RT_SUCCESSFUL) {
		return api_error(p, req, res, r);
	}

	struct user_info *ui = ythtbbs_cache_utmp_get_by_idx(get_user_utmp_index(sessid));

	char mybrd[GOOD_BRC_NUM][STRLEN];
	int mybrdnum;
	r = readmybrd(mybrd, &mybrdnum, ui->userid);

	if(mybrdnum >= GOOD_BRC_NUM) {
		return api_error(p, req, res, API_RT_REACHMAXRCD);
	}

	if(ismybrd(board, mybrd, mybrdnum)) {
		return api_error(p, req, res, API_RT_ALRDYINRCD);
	}

	struct boardmem * b = getboardbyname(board);
	if(b == NULL) {
		return api_error(p, req, res, API_RT_NOSUCHBRD);
	}


	if(!check_user_read_perm_x(ui, b)) {
		return api_error(p, req, res, API_RT_FBDNUSER);
	}

	// 所有校验通过，写入用户文件
	strcpy(mybrd[mybrdnum], b->header.filename);
	mybrdnum++;
	char file[200];
	sethomefile(file, ui->userid, ".goodbrd");
	FILE *fp = fopen(file, "w");
	if(!fp) {
		return api_error(p, req, res, API_RT_NOSUCHFILE);
	}

	flock(fileno(fp), LOCK_EX);
	int i;
	for(i=0; i<mybrdnum; i++) {
		fprintf(fp, "%s\n", mybrd[i]);
	}
	flock(fileno(fp), LOCK_UN);

	fclose(fp);

	api_set_json_header(res);
	onion_response_printf(res, "{\"errcode\": 0, \"board\": \"%s\", \"secstr\":\"%s\"}", b->header.filename, b->header.sec1);

	return OCS_PROCESSED;
}

int api_board_fav_del(ONION_FUNC_PROTO_STR)
{
	const char *board = onion_request_get_query(req, "board");
	const char *userid = onion_request_get_query(req, "userid");
	const char *appkey = onion_request_get_query(req, "appkey");
	const char *sessid = onion_request_get_query(req, "sessid");

	if(!board || !userid || !appkey || !sessid)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	if(strcasecmp(userid, "guest")==0)
		return api_error(p, req, res, API_RT_NOTLOGGEDIN);

	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_NOSUCHUSER);

	int r = check_user_session(ue, sessid, appkey);
	free(ue);
	if(r != API_RT_SUCCESSFUL) {
		return api_error(p, req, res, r);
	}

	struct user_info *ui = ythtbbs_cache_utmp_get_by_idx(get_user_utmp_index(sessid));

	char mybrd[GOOD_BRC_NUM][STRLEN];
	int mybrdnum;
	r = readmybrd(mybrd, &mybrdnum, ui->userid);

	if(mybrdnum == 0) {
		return api_error(p, req, res, API_RT_NOTINRCD);
	}

	if(!ismybrd(board, mybrd, mybrdnum)) {
		return api_error(p, req, res, API_RT_NOTINRCD);
	}

	// 所有校验通过，写入用户文件
	char file[200];
	sethomefile(file, ui->userid, ".goodbrd");
	FILE *fp = fopen(file, "w");
	if(!fp) {
		return api_error(p, req, res, API_RT_NOSUCHFILE);
	}

	flock(fileno(fp), LOCK_EX);
	int i;
	for(i=0; i<mybrdnum; i++) {
		if(strcasecmp(mybrd[i], board) == 0)
			continue;

		fprintf(fp, "%s\n", mybrd[i]);
	}
	flock(fileno(fp), LOCK_UN);

	fclose(fp);

	api_set_json_header(res);
	onion_response_printf(res, "{\"errcode\": 0, \"board\": \"%s\"}", board);

	return OCS_PROCESSED;
}

int api_board_fav_list(ONION_FUNC_PROTO_STR)
{
	const char *userid = onion_request_get_query(req, "userid");
	const char *appkey = onion_request_get_query(req, "appkey");
	const char *sessid = onion_request_get_query(req, "sessid");

	if(!userid || !appkey || !sessid)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	if(strcasecmp(userid, "guest")==0)
		return api_error(p, req, res, API_RT_NOTLOGGEDIN);

	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_NOSUCHUSER);

	int r = check_user_session(ue, sessid, appkey);
	free(ue);
	if(r != API_RT_SUCCESSFUL) {
		return api_error(p, req, res, r);
	}

	struct user_info *ui = ythtbbs_cache_utmp_get_by_idx(get_user_utmp_index(sessid));

	char mybrd[GOOD_BRC_NUM][STRLEN];
	int mybrdnum;
	r = readmybrd(mybrd, &mybrdnum, ui->userid);

	// 输出
	char buf[512];
	sprintf(buf, "{\"errcode\":0, \"board_num\":%d, \"board_array\":[]}", mybrdnum);
	struct json_object * obj = json_tokener_parse(buf);
	struct json_object * json_array_board = json_object_object_get(obj, "board_array");

	int i=0;
	struct boardmem * b = NULL;
	for(i=0; i<mybrdnum; ++i) {
		struct json_object * item = json_object_new_object();
		json_object_object_add(item, "name", json_object_new_string(mybrd[i]));

		b = getboardbyname(mybrd[i]);
		if(b == NULL) {
			json_object_object_add(item, "accessible", json_object_new_int(0));
		} else {
			json_object_object_add(item, "accessible", json_object_new_int(check_user_read_perm_x(ui, b)));
		}
		json_object_object_add(item, "secstr", json_object_new_string(b->header.sec1));

		json_object_array_add(json_array_board, item);
	}

	api_set_json_header(res);
	onion_response_write0(res, json_object_to_json_string(obj));

	json_object_put(obj);
	return OCS_PROCESSED;
}

static int api_board_autocomplete_callback(struct boardmem *board, int curr_idx, va_list ap) {
	const char *search_str = va_arg(ap, const char *);
	struct user_info *ui = va_arg(ap, struct user_info *);
	struct json_object *json_array_board = va_arg(ap, struct json_object *);

	if (board->header.filename[0] <= 32 || board->header.filename[0] > 'z')
		return 0;

	if (!check_user_read_perm_x(ui, board))
		return 0;

	if (strcasestr(board->header.filename, search_str)) {
		struct json_object *obj = json_object_new_object();
		json_object_object_add(obj, "name", json_object_new_string(board->header.filename));
		json_object_object_add(obj, "secstr", json_object_new_string(board->header.sec1));
		json_object_array_add(json_array_board, obj);
	}

	return 0;
}

int api_board_autocomplete(ONION_FUNC_PROTO_STR)
{
	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");
	const char * search_str = onion_request_get_query(req, "search_str");

	if(!userid || !sessid || !appkey || !search_str)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	if(strlen(search_str) < 2)
		return api_error(p, req, res, API_RT_SUCCESSFUL);

	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_NOSUCHUSER);

	int r = check_user_session(ue, sessid, appkey);
	free(ue);
	if(r != API_RT_SUCCESSFUL) {
		return api_error(p, req, res, r);
	}

	struct user_info *ui = ythtbbs_cache_utmp_get_by_idx(get_user_utmp_index(sessid));
	struct json_object *obj = json_tokener_parse("{\"errcode\":0, \"board_array\":[]}");
	struct json_object *json_array_board = json_object_object_get(obj, "board_array");

	ythtbbs_cache_Board_foreach_v(api_board_autocomplete_callback, search_str, ui, json_array_board);
	api_set_json_header(res);
	onion_response_write0(res, json_object_to_json_string(obj));

	json_object_put(obj);

	return OCS_PROCESSED;
}

static int api_board_list_fav(ONION_FUNC_PROTO_STR)
{
	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");
	const char * sortmode_s = onion_request_get_query(req, "sortmode");
	const char * fromhost = onion_request_get_header(req, "X-Real-IP");

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
	struct user_info *ui = ythtbbs_cache_utmp_get_by_idx(uent_index);
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
	api_set_json_header(res);
	onion_response_write0(res, s);
	free(ue);
	free(s);
	return OCS_PROCESSED;
}

static int api_board_list_sec(ONION_FUNC_PROTO_STR)
{
	const char * secstr = onion_request_get_query(req, "secstr");
	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");
	const char * sortmode_s = onion_request_get_query(req, "sortmode");
	const char * fromhost = onion_request_get_header(req, "X-Real-IP");
	//const char guest_str[] = "guest";
	const char sec_array[] = "0123456789GNHAC";

	if(secstr == NULL || strlen(secstr)>=2 || strstr(sec_array, secstr) == NULL)
		return api_error(p, req, res, API_RT_WRONGPARAM);
	if(!userid)
		return api_error(p, req, res, API_RT_WRONGPARAM); // TODO
		//return api_board_list_sec_guest(p, req, res);
	if(!sessid || !appkey)
		return api_error(p, req, res, API_RT_WRONGPARAM);
	int sortmode = (sortmode_s) ? atoi(sortmode_s) : 2;
	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_NOSUCHUSER);
	int r = check_user_session(ue, sessid, appkey);
	if(r != API_RT_SUCCESSFUL) {
		free(ue);
		return api_error(p, req, res, r);
	}

	struct boardmem *board_array[MAXBOARD], *x;
	int len,  hasintro = 0, i = 0, count = 0;
	const struct sectree *sec;
	int uent_index = get_user_utmp_index(sessid);
	struct user_info *ui = ythtbbs_cache_utmp_get_by_idx(uent_index);
	sec = getsectree(secstr);
	len = strlen(secstr);
	if(sec->introstr[0])
		hasintro = 1;
	for(i=0; i<MAXBOARD && i<shm_bcache->number; ++i) {
		x = &(shm_bcache->bcache[i]);
		if(x->header.filename[0]<=32 || x->header.filename[0]>'z')
			continue;
		if(hasintro){
			if(strcmp(secstr, x->header.sec1) && strcmp(secstr, x->header.sec2))
				continue;
		}else{
			if(strncmp(secstr, x->header.sec1, len) && strncmp(secstr, x->header.sec2, len))
				continue;
		}
		if(!check_user_read_perm_x(ui, x))
			continue;
		board_array[count] = x;
		count++;
	}
	char *s = bmy_board_array_to_json_string(board_array, count, sortmode, fromhost, ui);
	api_set_json_header(res);
	onion_response_write0(res, s);
	free(ue);
	free(s);
	return OCS_PROCESSED;
}

static int api_board_list_sec_guest(ONION_FUNC_PROTO_STR)
{
	return 0;
/*
	int guest_uid = getusernum("guest");
	int guest_uent_index, i;
	struct user_info *ui=NULL;
	for(i=0; i<6; i++) {
		guest_uent_index = shm_uindex->user[guest_uid][i];
		ui = ythtbbs_cache_utmp_get_by_idx(guest_uent_index-1); // TODO
		if(strcasecmp(ui->userid, "guest")==0)
			break;
	}

	if(strcasecmp(ui->userid, "guest")!=0)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	const char * secstr = onion_request_get_query(req, "secstr");
	const char * sortmode_s = onion_request_get_query(req, "sortmode");
	const char * fromhost = onion_request_get_header(req, "X-Real-IP");

	int sortmode = (sortmode_s) ? atoi(sortmode_s) : 2;
	int len, hasintro=0, count=0;

	struct boardmem *board_array[MAXBOARD], *x;
	struct sectree *sec = getsectree(secstr);
	len = strlen(secstr);
	if(sec->introstr[0])
		hasintro = 1;
	for(i=0; i<MAXBOARD && i<shm_bcache->number; ++i) {
		x = &(shm_bcache->bcache[i]);
		if(x->header.filename[0]<=32 || x->header.filename[0]>'z')
			continue;
		if(hasintro) {
			if(strcmp(secstr, x->header.sec1) && strcmp(secstr, x->header.sec2))
				continue;
		} else {
			if(strncmp(secstr, x->header.sec1, len) && strncmp(secstr, x->header.sec2, len))
				continue;
		}
		if(!check_user_read_perm_x(ui, x))
			continue;
		board_array[count] = x;
		count++;
	}

	char *s = bmy_board_array_to_json_string(board_array, count, sortmode, fromhost, ui);
	api_set_json_header(res);
	onion_response_write0(res, s);
	free(s);
	return OCS_PROCESSED;
*/
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
				!board_read(bp->header.filename, bp->lastpost, fromhost, ui),
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
