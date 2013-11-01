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

/**
 * @brief 将十大、分区热门话题转为 JSON 数据输出
 * @param mode 0 为十大模式，1 为分区模式
 * @param secstr 分区字符
 * @return
 */
static int api_article_list_xmltopfile(ONION_FUNC_PROTO_STR, int mode, const char *secstr);

/**
 * @brief 将美文推荐，或通知公告转为JSON数据输出
 * @param mode 0 为美文推荐，1为通知公告 
 * @return 同上
 */
static int api_article_list_commend(ONION_FUNC_PROTO_STR, int mode);

/**
 * @brief 通过版面名，文章ID，查找对应主题ID
 * @param board : board name 
 * @param filetime : file id
 * @return thread id; return 0 means not find the thread id
 */
static int get_thread_by_filetime(char *board, int filetime);

/**
 * @brief 通过主题ID查找同主题文章数量
 * @param thread : the thread id
 * @return the nubmer of articles in the thread
 */
static int get_number_of_articles_in_thread(char *board, int thread);

int api_article_list(ONION_FUNC_PROTO_STR)
{
	const char *type = onion_request_get_query(req, "type");

	if(strcasecmp(type, "top10")==0) { // 十大
		return api_article_list_xmltopfile(p, req, res, 0, NULL);
	} else if(strcasecmp(type, "sectop")==0) { // 分区热门
		const char *secstr = onion_request_get_query(req, "secstr");
		if(secstr == NULL)
			return api_error(p, req, res, API_RT_WRONGPARAM);
		if(strlen(secstr)>2)
			return api_error(p, req, res, API_RT_WRONGPARAM);

		return api_article_list_xmltopfile(p, req, res, 1, secstr);
	} else if(strcasecmp(type, "commend")==0) { // 美文推荐
		return api_article_list_commend(p, req, res, 0);
	} else if(strcasecmp(type, "announce")==0) { // 通知公告
		return api_article_list_commend(p, req, res, 1); 
	} else if(strcasecmp(type, "board")==0) { // 版面文章
		return OCS_NOT_IMPLEMENTED;
	} else if(strcasecmp(type, "thread")==0) { // 同主题列表
		return OCS_NOT_IMPLEMENTED;
	} else
		return api_error(p, req, res, API_RT_WRONGPARAM);
}

static int api_article_list_xmltopfile(ONION_FUNC_PROTO_STR, int mode, const char *secstr)
{
	int listmax;
	char ttfile[40];
	if(mode == 0) { // 十大热门
		listmax = 10;
		sprintf(ttfile, "wwwtmp/ctopten");
	} else { // 分区热门
		listmax = 5;
		sprintf(ttfile, "etc/Area_Dir/%s", secstr);
	}

	struct bmy_article top_list[listmax];
	memset(top_list, 0, sizeof(top_list[0]) * listmax);

	htmlDocPtr doc = htmlParseFile(ttfile, "GBK");
	if(doc==NULL)
		return api_error(p, req, res, API_RT_NOTOP10FILE);

	char xpath_links[40], xpath_nums[16];

	if(mode == 0) { //十大
		sprintf(xpath_links, "//div[@class='td-overflow']/a");
		sprintf(xpath_nums, "//tr/td[4]");
	} else { //分区
		sprintf(xpath_links, "//div[@class='bd-overflow']/a");
		sprintf(xpath_nums, "//tr/td[3]");
	}

	xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
	if(ctx==NULL) {
		xmlFreeDoc(doc);
		return api_error(p, req, res, API_RT_XMLFMTERROR);
	}

	xmlXPathObjectPtr r_links = xmlXPathEvalExpression((const xmlChar*)xpath_links, ctx);
	xmlXPathObjectPtr r_nums = xmlXPathEvalExpression((const xmlChar*)xpath_nums, ctx);

	if(r_links->nodesetval == 0 || r_nums->nodesetval == 0) {
		xmlXPathFreeObject(r_links);
		xmlXPathFreeObject(r_nums);
		xmlXPathFreeContext(ctx);
		xmlFreeDoc(doc);
		return api_error(p, req, res, API_RT_XMLFMTERROR);
	}

	int total = r_links->nodesetval->nodeNr;
	if( total == 0 || total>listmax ||
		(mode == 0 && r_nums->nodesetval->nodeNr - total != 1) ||
		(mode == 1 && r_nums->nodesetval->nodeNr != total)) {
		xmlXPathFreeObject(r_links);
		xmlXPathFreeObject(r_nums);
		xmlXPathFreeContext(ctx);
		xmlFreeDoc(doc);
		return api_error(p, req, res, API_RT_XMLFMTERROR);
	}

	int i;
	xmlNodePtr cur_link, cur_num;
	char *link, *num, *t1, *t2, buf[256], tmp[16];
	for(i=0; i<total; ++i) {
		cur_link = r_links->nodesetval->nodeTab[i];
		if(mode==0)
			cur_num = r_nums->nodesetval->nodeTab[i+1];
		else
			cur_num = r_nums->nodesetval->nodeTab[i];

		link = (char *)xmlGetProp(cur_link, (const xmlChar*)"href");
		num = (char *)xmlNodeGetContent(cur_num);

		top_list[i].type = (strstr(link, "tfind?board") != NULL);
		if(mode == 0) {
			top_list[i].th_num = atoi(num);
		} else {
			// 分区模式下num格式为 (7)
			snprintf(tmp, strlen(num)-1, "%s", num+1);
			top_list[i].th_num = atoi(tmp);
		}
		strncpy(top_list[i].title, (const char*)xmlNodeGetContent(cur_link), 80);

		memset(buf, 0, 256);
		memcpy(buf, link, 256);

		t1 = strtok(buf, "&");
		t2 = strchr(t1, '=');
		t2++;
		sprintf(top_list[i].board, "%s", t2);

		t1 = strtok(NULL, "&");
		t2 = strchr(t1, '=');

		if(top_list[i].type) {
			t2++;
			sprintf(tmp, "%s", t2);
			top_list[i].thread = atoi(tmp);
		} else {
			t2=t2+3;
			snprintf(tmp, 11, "%s", t2);
			top_list[i].filetime = atoi(tmp);
		}

	}

	char *s = bmy_article_array_to_json_string(top_list, total);

	xmlXPathFreeObject(r_links);
	xmlXPathFreeObject(r_nums);
	xmlXPathFreeContext(ctx);
	xmlFreeDoc(doc);

	onion_response_set_header(res, "Content-type", "application/json; charset=utf-8");
	onion_response_write0(res, s);

	free(s);

	return OCS_PROCESSED;
}

static int api_article_list_commend(ONION_FUNC_PROTO_STR, int mode)
{
	const int max_list_number = 20;
	struct bmy_article commend_list[max_list_number];
	struct commend x;
	memset(commend_list, 0, sizeof(commend_list[0]) * max_list_number);

	FILE *fp = NULL;
	if(0 == mode)
		fp = fopen(".COMMEND", "r");
	else if(1 == mode)
		fp = fopen(".COMMEND2", "r");
	if(!fp)
		return api_error(p, req, res, API_RT_NOCOMMENDFILE);


	fseek(fp, -20*sizeof(struct commend), SEEK_END);
	int count=0, length = 0, i;
	for(i=0; i<max_list_number; i++) {
		if(fread(&x, sizeof(struct commend), 1, fp)<=0)
			break;
		if(x.accessed & FH_ALLREPLY)
			commend_list[i].mark = x.accessed;
		commend_list[i].type = 0;
		length = strlen(x.title);
		g2u(x.title, length, commend_list[i].title, length);
		strcpy(commend_list[i].author, x.userid);
		strcpy(commend_list[i].board, x.board);
		commend_list[i].filetime = 0;
		char *p_filename = (char *)x.filename;
		while(*p_filename != 0 && (*p_filename > '9' || *p_filename < '0'))
			++p_filename;
		while(*p_filename != 0 && '0' <= *p_filename && *p_filename <= '9')
		{
			commend_list[i].filetime = commend_list[i].filetime * 10 + (*p_filename) - '0';
			++p_filename;
		}
		commend_list[i].thread = get_thread_by_filetime(commend_list[i].board, commend_list[i].filetime);
		commend_list[i].th_num = get_number_of_articles_in_thread(commend_list[i].board, commend_list[i].thread);
		++count;
	}
	fclose(fp);
	char *s = bmy_article_array_to_json_string(commend_list, count);
	onion_response_set_header(res, "Content-type", "application/json; charset=utf-8");
	onion_response_write0(res, s);
	free(s);
	return OCS_PROCESSED;
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
		sprintf(buf, "{ \"type\":%d, \"board\":\"%s\", \"aid\":%d, \"tid\":%d, "
				"\"title\":\"%s\", \"author\":\"%s\", \"th_num\":%d, \"mark\":%d }",
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

static int get_thread_by_filetime(char *board, int filetime)
{
	char dir[80];
	struct mmapfile mf = { ptr:NULL };
	struct fileheader fh, *p_fh;

	sprintf(dir, "boards/%s/.DIR", board);
	MMAP_TRY{
		if(mmapfile(dir, &mf) == -1)
		{
			MMAP_UNTRY;
			return 0;
		}
		int total;
		total = mf.size / sizeof(struct fileheader);
		if(total == 0){
			mmapfile(NULL, &mf);
			MMAP_UNTRY;
			return 0;
		}
		int num = Search_Bin(mf.ptr, filetime, 0, total - 1);
		p_fh = (struct fileheader *)(mf.ptr + num * sizeof(struct fileheader));
		memcpy(&fh, p_fh, sizeof(struct fileheader));
		mmapfile(NULL, &mf);
		return fh.thread;
	}
	MMAP_CATCH{
		mmapfile(NULL, &mf);
		return 0;
	}
	MMAP_END mmapfile(NULL, &mf);
	return 0;
}

static int get_number_of_articles_in_thread(char *board, int thread)
{
	char dir[80];
	int i = 0, num_in_thread = 0, num_records = 0;
	struct mmapfile mf = { ptr:NULL };
	if(NULL == board)
		return 0;
	sprintf(dir, "boards/%s/.DIR",board);
	MMAP_TRY
	{
		if(-1 == mmapfile(dir, &mf))
		{
			MMAP_UNTRY;
			return 0;
		}
		num_records = mf.size / sizeof(struct fileheader);
		if(0 != thread)
		{
			i = Search_Bin(mf.ptr, thread, 0, num_records - 1);
			if(i < 0)
				i = -(i + 1);
		}
		else
			i = 0;
		for(; i < num_records; ++i)
		{
			if(((struct fileheader *)(mf.ptr + i * sizeof(struct fileheader)))->thread != thread)
				continue;
			else
				++num_in_thread;
		}
		mmapfile(NULL, &mf);
		return num_in_thread;
	}
	MMAP_CATCH
	{
		num_in_thread = 0;
	}
	MMAP_END mmapfile(NULL, &mf);
	return 0;
}
