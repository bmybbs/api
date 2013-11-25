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
 * @param board 版面名
 * @param mode 0 为美文推荐，1为通知公告
 * @param startnum 输出的第一篇文章序号，默认为(最新的文章-number)
 * @param number 总共输出的文章数，暂时默认为20
 * @return 返回json格式的查询结果
 */
static int api_article_list_commend(ONION_FUNC_PROTO_STR, int mode, int startnum, int number);

/**
 * @brief 将版面文章列表转为JSON数据输出
 * @param board 版面名
 * @param mode 0为一般模式， 1为主题模式
 * @param startnum 输出的第一篇文章序号，默认为(最新的文章-number)
 * @param number 总共输出的文章数，由用户设定，暂时默认为20
 * @return 返回json格式的查询结果
 */
static int api_article_list_board(ONION_FUNC_PROTO_STR);

/**
 * @brief 将同主题文章列表转为JSON数据输出
 * @param board 版面名
 * @param thread 主题ID
 * @param startnum 输出的第一篇文章序号，默认为(1)
 * @param number 总共输出的文章数，由用户设定，默认为全部内容
 * @return 返回json格式的查询结果
 */
static int api_article_list_thread(ONION_FUNC_PROTO_STR);

enum API_POST_TYPE {
	API_POST_TYPE_POST,		///< 发帖模式
	API_POST_TYPE_REPLY		///< 回帖模式
};

/**
 * @brief 实际处理发文的接口。
 * 使用 api_article_post 和 api_article_reply 封装。
 * @param mode 参见 API_POST_TYPE
 * @return
 */
static int api_article_do_post(ONION_FUNC_PROTO_STR, int mode);

/**
 * @brief 实际处理发文的函数。
 * 该函数来自 nju09。
 * @param board 版面名称
 * @param title 文章标题
 * @param filename 位于 bbstmpfs 中的文章内容
 * @param id 用于显示的作者 id
 * @param nickname 作者昵称
 * @param ip 来自 ip
 * @param sig 选用的签名档数字
 * @param mark fileheader 的标记
 * @param outgoing 是否转信
 * @param realauthor 实际的作者 id
 * @param thread 主题编号
 * @return 返回文件名中实际使用的时间戳
 */
static int do_article_post(char *board, char *title, char *filename, char *id,
		char *nickname, char *ip, int sig, int mark,
		int outgoing, char *realauthor, int thread);

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

/**
 * @brief 获取文章内容。
 * api_article_getHTMLContent() 和 api_article_getRAWContent() 放个方法
 * 实际上是这个方法的封装，通过 mode 参数进行区别。
 * article/getHTMLContent 和 article/getRAWContent 两个接口单独区分开，意
 * 在强调在做修改文章操作时，<strong>应当</strong>调用 getRAWcontent。
 * @param mode 参见 enum article_parse_mode
 * @return
 */
static int api_article_get_content(ONION_FUNC_PROTO_STR, int mode);

/**
 * @brief 从 .DIR 中依据 filetime 寻找文章对应的 fileheader 数据
 * @param mf 映射到内存中的 .DIR 文件内容
 * @param filetime 文件的时间戳
 * @param num
 * @param mode 1表示 .DIR 按时间排序，0表示不排序
 * @return
 */
static struct fileheader * findbarticle(struct mmapfile *mf, int filetime, int *num, int mode);

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
		const char *str_start = onion_request_get_query(req, "startnum");
		const char *str_number = onion_request_get_query(req, "count");
		int start = 0, number = 0;
		if(NULL != str_start)
			start = atoi(str_start);
		if(NULL != str_number)
			number = atoi(str_number);
		return api_article_list_commend(p, req, res, 0, start, number); 

	} else if(strcasecmp(type, "announce")==0) { // 通知公告
		const char *str_start = onion_request_get_query(req, "startnum");
		const char *str_number = onion_request_get_query(req, "count");
		int start = 0, number = 0;
		if(NULL != str_start)
			start = atoi(str_start);
		if(NULL != str_number)
			number = atoi(str_number);
		return api_article_list_commend(p, req, res, 1, start, number);

	} else if(strcasecmp(type, "board")==0) { // 版面文章
		return api_article_list_board(p, req, res);

	} else if(strcasecmp(type, "thread")==0) { // 同主题列表
		return api_article_list_thread(p, req, res);
	} else
		return api_error(p, req, res, API_RT_WRONGPARAM);
}

int api_article_getHTMLContent(ONION_FUNC_PROTO_STR)
{
	return api_article_get_content(p, req, res, ARTICLE_PARSE_WITH_ANSICOLOR);
}

int api_article_getRAWContent(ONION_FUNC_PROTO_STR)
{
	return api_article_get_content(p, req, res, ARTICLE_PARSE_WITHOUT_ANSICOLOR);
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

static int api_article_list_commend(ONION_FUNC_PROTO_STR, int mode, int startnum, int number)
{
	if(0 >= number)
		number = 20;
	struct bmy_article commend_list[number];
	struct commend x;
	memset(commend_list, 0, sizeof(commend_list[0]) * number);
	char dir[80];
	FILE *fp = NULL;
	if(0 == mode)
		strcpy(dir, ".COMMEND");
	else if(1 == mode)
		strcpy(dir, ".COMMEND2");
	int fsize = file_size(dir);
	int total = fsize / sizeof(struct commend);
	fp = fopen(dir, "r");

	if(!fp || fsize == 0)
		return api_error(p, req, res, API_RT_NOCMMNDFILE);
	if(startnum == 0)
		startnum = total- number + 1;
	if(startnum <= 0)
		startnum = 1;

	fseek(fp, (startnum - 1) * sizeof(struct commend), SEEK_SET);
	int count=0, length = 0, i;
	for(i=0; i<number; i++) {
		if(fread(&x, sizeof(struct commend), 1, fp)<=0)
			break;
		if(x.accessed & FH_ALLREPLY)
			commend_list[i].mark = x.accessed;
		commend_list[i].type = 0;
		length = strlen(x.title);
		g2u(x.title, length, commend_list[i].title, 80);
		strcpy(commend_list[i].author, x.userid);
		strcpy(commend_list[i].board, x.board);
		commend_list[i].filetime = atoi((char *)x.filename + 2);
		commend_list[i].thread = get_thread_by_filetime(commend_list[i].board, commend_list[i].filetime);
		commend_list[i].th_num = get_number_of_articles_in_thread(commend_list[i].board, commend_list[i].thread);
		commend_list[i].type = 0;
		++count;
	}
	fclose(fp);
	char *s = bmy_article_array_to_json_string(commend_list, count);
	onion_response_set_header(res, "Content-type", "application/json; charset=utf-8");
	onion_response_write0(res, s);
	free(s);
	return OCS_PROCESSED;
}

static int api_article_list_board(ONION_FUNC_PROTO_STR)
{
	const char * board        = onion_request_get_query(req, "board");
	const char * str_btype    = onion_request_get_query(req, "btype");
	const char * str_startnum = onion_request_get_query(req, "startnum");
	const char * str_count    = onion_request_get_query(req, "count");
	const char * userid   = onion_request_get_query(req, "userid");
	const char * appkey   = onion_request_get_query(req, "appkey");
	const char * sessid   = onion_request_get_query(req, "sessid");
	//判断必要参数
	if(!(board && str_btype && userid && appkey && sessid))
		return api_error(p, req, res, API_RT_WRONGPARAM);
	//if(strlen(sessid) != 32)
	//	return api_error(p, req, res, API_RT_WRONGPARAM);
	//TODO: 签名检查
	//...
	//判断版面访问权
	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_NOSUCHUSER);
	int r = check_user_session(ue, sessid, appkey);
	if(r != API_RT_SUCCESSFUL){
		free(ue);
		return api_error(p, req, res, r);
	}
	if(ue != NULL)
		free(ue);
	struct user_info *ui = &(shm_utmp->uinfo[get_user_utmp_index(sessid)]);
	struct boardmem *b   = getboardbyname(board);
	if(b == NULL) {
		free(ue);
		return api_error(p, req, res, API_RT_NOSUCHBRD);
	}
	if(!check_user_read_perm_x(ui, b)) {
		free(ue);
		return api_error(p, req, res, API_RT_NOBRDRPERM);
	}

	int mode = 0, startnum = 0, count = 0;
	if(str_startnum != NULL)
		startnum = atoi(str_startnum);
	if(str_count != NULL)
		count = atoi(str_count);
	if(0 >= count)
		count = 20;
	if(str_btype[0] == 't')
		mode = 1;
	else
		mode = 0;
	int fd = 0;
	struct bmy_article board_list[count];
	memset(board_list, 0, sizeof(board_list[0]) * count);
	struct fileheader *data = NULL, x2;
	char dir[80], filename[80];
	int i = 0, total = 0, total_article = 0;

	sprintf(dir, "boards/%s/.DIR", board);
	int fsize = file_size(dir);
	fd = open(dir, O_RDONLY);
	if(0 == fd || 0 == fsize) {
		free(ue);
		return api_error(p, req, res, API_RT_EMPTYBRD);
	}
	MMAP_TRY
	{
		data = mmap(NULL, fsize, PROT_READ, MAP_SHARED, fd, 0);
		close(fd);
		if((void *) -1 == data)
		{
			MMAP_UNTRY;
			free(ue);
			return api_error(p, req, res, API_RT_CNTMAPBRDIR);
		}
		total = fsize / sizeof(struct fileheader);
		if(0 == mode)
		{
			total_article = total;
		}
		else if(1 == mode)
		{
			total_article = 0;
			for(i = 0; i < total; ++i)
				if(data[i].thread == data[i].filetime)
					++total_article;
		
		}
		if(startnum == 0)
			startnum = total_article - count + 1;
		if(startnum <= 0)
			startnum = 1;
		int sum = 0, num = 0;
		for(i = 0; i < total; ++i)
		{
			if(0 == mode)
				++sum;
			else if(1 == mode && data[i].thread == data[i].filetime)
				++sum;
			if(sum < startnum || (1 == mode && data[i].thread != data[i].filetime))
				continue;
			while (data[i].sizebyte == 0)
			{
				sprintf(filename, "boards/%s/%s", board, fh2fname(&data[i]));
				data[i].sizebyte = numbyte(eff_size(filename));
				fd = open(dir, O_RDWR);
				if (fd < 0)
					break;
				flock(fd, LOCK_EX);
				lseek(fd, (startnum - 1 + i) * sizeof (struct fileheader),SEEK_SET);
				if (read(fd, &x2, sizeof (x2)) == sizeof (x2) && data[i].filetime == x2.filetime)
				{
					x2.sizebyte = data[i].sizebyte;
					lseek(fd, -1 * sizeof (x2), SEEK_CUR);
					write(fd, &x2, sizeof (x2));
				}
				flock(fd, LOCK_UN);
				close(fd);
				break;
			}
			board_list[num].mark = data[i].accessed;
			board_list[num].filetime = data[i].filetime;
			board_list[num].thread = data[i].thread;
			board_list[num].type = mode;
		
			strcpy(board_list[num].board, board);
			strcpy(board_list[num].author, data[i].owner);
			g2u(data[i].title, strlen(data[i].title), board_list[num].title, 80);
			++num;
			if(num >= count)
				break;
		}
		munmap(data, fsize);
		for(i = 0; i < num; ++i){
			board_list[i].th_num = get_number_of_articles_in_thread(board_list[i].board, board_list[i].thread);
		}
		char *s = bmy_article_array_to_json_string(board_list, num);
		onion_response_set_header(res, "Content-type", "application/json; charset=utf-8");
		onion_response_write0(res, s);
		free(ue);
		free(s);
		return OCS_PROCESSED;	
	}
	MMAP_CATCH
	{
		;
	}
	MMAP_END munmap(data, fsize);
	free(ue);
	return api_error(p, req, res, API_RT_CNTMAPBRDIR);
}

static int api_article_list_thread(ONION_FUNC_PROTO_STR)
{
	const char * board        = onion_request_get_query(req, "board");
	const char * str_thread   = onion_request_get_query(req, "thread");
	const char * str_startnum = onion_request_get_query(req, "startnum");
	const char * str_count    = onion_request_get_query(req, "count");
	const char * userid   = onion_request_get_query(req, "userid");
	const char * appkey   = onion_request_get_query(req, "appkey");
	const char * sessid   = onion_request_get_query(req, "sessid");
	//判断必要参数
	if(!(board && str_thread && userid && appkey && sessid))
		return api_error(p, req, res, API_RT_WRONGPARAM);
	int thread = atoi(str_thread);
	if(thread == 0)
		return api_error(p, req, res, API_RT_WRONGPARAM);
	//TODO: 签名检查
	//...
	//判断版面访问权
	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_NOSUCHUSER);
	int r = check_user_session(ue, sessid, appkey);
	if(r != API_RT_SUCCESSFUL){
		free(ue);
		return api_error(p, req, res, r);
	}
	if(ue != NULL)
		free(ue);
	struct user_info *ui = &(shm_utmp->uinfo[get_user_utmp_index(sessid)]);
	struct boardmem *b   = getboardbyname(board);
	if(b == NULL)
		return api_error(p, req, res, API_RT_NOSUCHBRD);
	if(!check_user_read_perm_x(ui, b))
		return api_error(p, req, res, API_RT_FBDNUSER);

	int startnum = 0, count = 0;
	if(str_startnum != NULL)
		startnum = atoi(str_startnum);
	if(str_count != NULL)
		count = atoi(str_count);

	int fd = 0;
	struct fileheader *data = NULL, x2;
	char dir[80], filename[80];
	int i = 0, total = 0, total_article = 0;
	sprintf(dir, "boards/%s/.DIR", board);
	int fsize = file_size(dir);
	fd = open(dir, O_RDONLY);
	if(0 == fd || 0 == fsize) {
		free(ue);
		return api_error(p, req, res, API_RT_EMPTYBRD);
	}
	MMAP_TRY
	{
		data = mmap(NULL, fsize, PROT_READ, MAP_SHARED, fd, 0);
		close(fd);
		if((void *) -1 == data)
		{
			MMAP_UNTRY;
			free(ue);
			return api_error(p, req, res, API_RT_CNTMAPBRDIR);
		}
		total = fsize / sizeof(struct fileheader);
		total_article = 0;
		for(i = 0; i < total; ++i)
		{
			if(data[i].thread == thread)
				++total_article;
		}
		if(count == 0)
			count = total_article;
		struct bmy_article board_list[count];
		memset(board_list, 0, sizeof(board_list[0]) * count);
		if(startnum == 0)
			startnum = total_article - count + 1;
		if(startnum <= 0)
			startnum = 1;
		int sum = 0, num = 0;
		for(i = 0; i < total; ++i)
		{
			if(data[i].thread != thread)
				continue;
			++sum;
			if(sum < startnum)
				continue;
			while (data[i].sizebyte == 0)
			{
				sprintf(filename, "boards/%s/%s", board, fh2fname(&data[i]));
				data[i].sizebyte = numbyte(eff_size(filename));
				fd = open(dir, O_RDWR);
				if (fd < 0)
					break;
				flock(fd, LOCK_EX);
				lseek(fd, (startnum - 1 + i) * sizeof (struct fileheader),SEEK_SET);
				if (read(fd, &x2, sizeof (x2)) == sizeof (x2) && data[i].filetime == x2.filetime)
				{
					x2.sizebyte = data[i].sizebyte;
					lseek(fd, -1 * sizeof (x2), SEEK_CUR);
					write(fd, &x2, sizeof (x2));
				}
				flock(fd, LOCK_UN);
				close(fd);
				break;
			}
			board_list[num].mark = data[i].accessed;
			board_list[num].filetime = data[i].filetime;
			board_list[num].thread = data[i].thread;
			board_list[num].type = 0;
		
			strcpy(board_list[num].board, board);
			strcpy(board_list[num].author, data[i].owner);
			g2u(data[i].title, strlen(data[i].title), board_list[num].title, 80);
			++num;
			if(num >= count)
				break;

		}
		munmap(data, fsize);
		for(i = 0; i < num; ++i){
			board_list[i].th_num = get_number_of_articles_in_thread(board_list[i].board, board_list[i].thread);
		}
		char *s = bmy_article_array_to_json_string(board_list, num);
		onion_response_set_header(res, "Content-type", "application/json; charset=utf-8");
		onion_response_write0(res, s);
		free(ue);
		free(s);
		return OCS_PROCESSED;	
	}
	MMAP_CATCH
	{
		;
	}
	MMAP_END munmap(data, fsize);
	free(ue);
	return api_error(p, req, res, API_RT_CNTMAPBRDIR);
}

static int api_article_get_content(ONION_FUNC_PROTO_STR, int mode)
{
	const char * bname = onion_request_get_query(req, "board");
	const char * aid_str = onion_request_get_query(req, "aid");

	if(!bname || !aid_str) {
		return api_error(p, req, res, API_RT_WRONGPARAM);
	}

	struct boardmem *bmem = getboardbyname(bname);
	if(!bmem)
		return api_error(p, req, res, API_RT_NOSUCHBRD);

	int aid = atoi(aid_str);

	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");

	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	if(check_user_session(ue, sessid, appkey) != API_RT_SUCCESSFUL) {
		free(ue);
		userid = "guest";	// session 不合法的情况下，userid 和 ue 转为 guest
		ue = getuser(userid);
	}

	int uent_index = get_user_utmp_index(sessid);
	struct user_info *ui = (strcasecmp(userid, "guest")==0) ?
			NULL : &(shm_utmp->uinfo[uent_index]);
	if(!check_user_read_perm_x(ui, bmem)) {
		free(ue);
		return api_error(p, req, res, API_RT_NOBRDRPERM);
	}

	// 删除回复提醒
	if(is_post_in_notification(ue->userid, bname, aid))
		del_post_notification(ue->userid, bname, aid);

	int total = bmem->total;
	if(total<=0) {
		free(ue);
		return api_error(p, req, res, API_RT_EMPTYBRD);
	}

	char dir_file[80], filename[80];
	struct fileheader *fh = NULL;
	sprintf(filename, "M.%d.A", aid);
	sprintf(dir_file, "boards/%s/.DIR", bname);

	struct mmapfile mf = { ptr:NULL };
	if(mmapfile(dir_file, &mf) == -1) {
		free(ue);
		return api_error(p, req, res, API_RT_EMPTYBRD);
	}

	const char * num_str = onion_request_get_query(req, "num");
	int num = (num_str == NULL) ? -1 : (atoi(num_str)-1);
	fh = findbarticle(&mf, aid, &num, 1);
	if(fh == NULL) {
		mmapfile(NULL, &mf);
		free(ue);
		return api_error(p, req, res, API_RT_NOSUCHATCL);
	}

	if(fh->owner[0] == '-') {
		mmapfile(NULL, &mf);
		free(ue);
		return api_error(p, req, res, API_RT_ATCLDELETED);
	}

	char title_utf8[180];
	memset(title_utf8, 0, 180);
	g2u(fh->title, strlen(fh->title), title_utf8, 180);

	struct attach_link *attach_link_list=NULL;
	char * article_content_utf8 = parse_article(bmem->header.filename,
			filename, mode, &attach_link_list);

	char * article_json_str = (char *)malloc(strlen(article_content_utf8) + 512);
	memset(article_json_str, 0, strlen(article_content_utf8) + 512);
	int curr_permission = !strncmp(ui->userid, fh->owner, IDLEN+1);
	sprintf(article_json_str, "{\"errcode\":0, \"attach\":[], "
			"\"can_edit\":%d, \"can_delete\":%d, \"can_reply\":%d, "
			"\"board\":\"%s\", \"author\":\"%s\", \"thread\":%d, \"num\":%d}",
			curr_permission, curr_permission,
			!(fh->accessed & FH_NOREPLY), bmem->header.filename,
			fh2owner(fh), fh->thread, num);

	struct json_object * jp = json_tokener_parse(article_json_str);
	json_object_object_add(jp, "content", json_object_new_string(article_content_utf8));
	if(attach_link_list) {
		struct json_object * attach_array = json_object_object_get(jp, "attach");
		char at_buf[320];
		struct attach_link * alp = attach_link_list;
		while(alp) {
			memset(at_buf, 0, 320);
			sprintf(at_buf, "{\"link\":\"%s\", \"size\":%d}", alp->link, alp->size);
			json_object_array_add(attach_array, json_tokener_parse(at_buf));
			alp=alp->next;
		}
	}

	char * api_output = strdup(json_object_to_json_string(jp));

	free(ue);
	mmapfile(NULL, &mf);
	free(article_content_utf8);
	free(article_json_str);
	json_object_put(jp);
	free_attach_link_list(attach_link_list);

	onion_response_set_header(res, "Content-type", "application/json; charset=utf-8");
	onion_response_write0(res, api_output);
	free(api_output);
	return OCS_PROCESSED;
}

int api_article_post(ONION_FUNC_PROTO_STR)
{
	return api_article_do_post(p, req, res, API_POST_TYPE_POST);
}

int api_article_reply(ONION_FUNC_PROTO_STR)
{
	return api_article_do_post(p, req, res, API_POST_TYPE_REPLY);
}

static int api_article_do_post(ONION_FUNC_PROTO_STR, int mode)
{
	if((onion_request_get_flags(req)&OR_METHODS) != OR_POST)
		return api_error(p, req, res, API_RT_WRONGMETHOD);

	const char * board = onion_request_get_query(req, "board");
	const char * title = onion_request_get_query(req, "title");
	const char * ref_str = onion_request_get_query(req, "ref"); // 回复的文章时间
	const char * rid_str = onion_request_get_query(req, "rid");	// 回复的文章编号
	const char * th_str = onion_request_get_query(req, "th");	// 回复的主题

	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");
	const char * token  = onion_request_get_query(req, "token");

	if(!board || !title || !userid || !sessid || !appkey || !token)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	if(mode==API_POST_TYPE_REPLY && (!ref_str || !rid_str || !th_str))
		return api_error(p, req, res, API_RT_WRONGPARAM);

	if(title[0]==0)
		return api_error(p, req, res, API_RT_ATCLNOTITLE);

	struct userec *ue = getuser(userid);
	if(ue==NULL)
		return api_error(p, req, res, API_RT_NOSUCHUSER);

	if(check_user_session(ue, sessid, appkey) != API_RT_SUCCESSFUL) {
		free(ue);
		return api_error(p, req, res, API_RT_WRONGSESS);
	}

	const char * fromhost = onion_request_get_client_description(req);

	struct boardmem * bmem = getboardbyname(board);
	if(bmem==NULL) {
		free(ue);
		return api_error(p, req, res, API_RT_NOSUCHBRD);
	}

	int thread = -1;
	int mark;
	char noti_userid[14] = { '\0' };
	if(API_POST_TYPE_REPLY) { // 已通过参数校验
		char dir[80];
		sprintf(dir, "boards/%s/.DIR", bmem->header.filename);
		int ref = atoi(ref_str);
		int rid = atoi(rid_str);

		struct mmapfile mf = { ptr:NULL };
		if(mmapfile(dir, &mf) == -1) {
			free(ue);
			return api_error(p, req, res, API_RT_CNTMAPBRDIR);
		}

		struct fileheader *x = findbarticle(&mf, ref, &rid, 1);

		if(x->accessed & FH_NOREPLY) {
			mmapfile(NULL, &mf);
			free(ue);
			return api_error(p, req, res, API_RT_ATCLFBDREPLY);
		}

		if(x && (x->accessed & FH_ALLREPLY)) {
			mark |= FH_ALLREPLY;
		}

		if(x) {
			thread = x->thread;
			if(strchr(x->owner, '.') == NULL) {
				if(x->owner == 0) {
					memcpy(noti_userid, &x->owner[1], IDLEN);
				} else {
					memcpy(noti_userid, x->owner, IDLEN);
				}
			}
		} else {
			thread = -1;
		}

		mmapfile(NULL, &mf);
	}

	int uent_index = get_user_utmp_index(sessid);
	struct user_info *ui = &(shm_utmp->uinfo[uent_index]);

	if(!check_user_post_perm_x(ui, bmem)) {
		free(ue);
		return api_error(p, req, res, API_RT_NOBRDPPERM);
	}

	if(strcmp(ui->token, token) !=0 ) {
		free(ue);
		return api_error(p, req, res, API_RT_WRONGTOKEN);
	}

	if(!strcasecmp(ue->userid, "guest") && seek_in_file(MY_BBS_HOME "/etc/guestbanip", fromhost)) {
		free(ue);
		return api_error(p, req, res, API_RT_FBDGSTPIP);
	}

	const onion_block * http_req_body = onion_request_get_data(req);
	const char *data = onion_block_data(http_req_body);

	char filename[80];
	sprintf(filename, "bbstmpfs/tmp/%s_%s.tmp", ue->userid, appkey); // line:141

	char * data_gbk = (char *)malloc(strlen(data)*2);
	memset(data_gbk, 0, strlen(data)*2);
	u2g(data, strlen(data), data_gbk, strlen(data)*2);

	while(strstr(data_gbk, "[ESC]")!=NULL)
		data_gbk = string_replace(data_gbk, "[ESC]", "\033");

	f_write(filename, data_gbk);
// 	TODO: free(data_gbk);

	int is_anony = strlen(onion_request_get_query(req, "anony"));
	int is_norep = strlen(onion_request_get_query(req, "norep"));
	if(is_norep)
		mark |= FH_NOREPLY;

	if(is_anony && (bmem->header.flag & ANONY_FLAG))
		is_anony = 1;
	else
		is_anony = 0;

	int is_1984 = (bmem->header.flag & IS1984_FLAG) ? 1 : 0;

	char * title_gbk = (char *)malloc(strlen(title)*2);
	memset(title_gbk, 0, strlen(title)*2);
	u2g(title, strlen(title), title_gbk, strlen(title)*2);
	int i;
	for(i=0;i<strlen(title_gbk);++i) {
		if(title_gbk[i]<=27 && title_gbk[i]>=-1)
			title_gbk[i] = ' ';
	}
	i = strlen(title_gbk) - 1;
	while(i>0 && isspace(title_gbk[i]))
		title_gbk[i--] = 0;

	// TODO: 处理签名档

	// TODO: 缺少 nju09/bbssnd.c:143 有关报警的逻辑

	//if(insertattachments(filename, data_gbk, ue->userid))
		//mark = mark | FH_ATTACHED;

	int r;
	if(is_anony) {
		r = do_article_post(bmem->header.filename, title_gbk, filename, "Anonymous",
				"我是匿名天使", "匿名天使的家", 0, mark,
				0, ui->userid, thread);
	} else {
		r = do_article_post(bmem->header.filename, title_gbk, filename, ui->userid,
				ui->username, fromhost, 0, mark,
				0, ui->userid, thread);
	}

	if(r<=0) {
		free(ue);
		free(title_gbk);
		free(data_gbk);
		unlink(filename);
		api_error(p, req, res, API_RT_ATCLINNERR);
	}

	// TODO: 更新未读标记
	//brc_initial

	unlink(filename);

	char buf[256];
	sprintf(buf, "%s post %s %s", ui->userid, bmem->header.filename, title_gbk);
	newtrace(buf);

	if(bmem->header.clubnum == 0 && !board_is_junkboard(bmem->header.filename)) {
		ue->numposts++;
		save_user_data(ue);
	}

	//回帖提醒
	if(API_POST_TYPE_REPLY && !strcmp(ue->userid, noti_userid)) {
		add_post_notification(noti_userid, (is_anony) ? "Anonymous" : ue->userid,
				bmem->header.filename, r, title_gbk);
	}

	free(ue);
	free(title_gbk);
	free(data_gbk);
	getrandomstr_r(ui->token, TOKENLENGTH+1);
	memset(ui->from, 0, 20);
	strncpy(ui->from, fromhost, 20);
	onion_response_set_header(res, "Content-type", "application/json; charset=utf-8");
	onion_response_printf(res, "{ \"errcode\":0, \"aid\":%d, \"token\":\"%s\" }",
			r, ui->token);

	return OCS_NOT_IMPLEMENTED;
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
		sprintf(buf, "{ \"type\":%d, \"aid\":%d, \"tid\":%d, "
				"\"th_num\":%d, \"mark\":%d }",
				p->type, p->filetime, p->thread, p->th_num, p->mark);
		jp = json_tokener_parse(buf);
		if(jp) {
			json_object_object_add(jp, "board", json_object_new_string(p->board));
			json_object_object_add(jp, "title", json_object_new_string(p->title));
			json_object_object_add(jp, "author", json_object_new_string(p->author));
			json_object_array_add(json_array, jp);
		}
	}

	char *r = strdup(json_object_to_json_string(obj));
	json_object_put(obj);

	return r;
}

static int get_thread_by_filetime(char *board, int filetime)
{
	char dir[80];
	struct mmapfile mf = { ptr:NULL };
	struct fileheader *p_fh;
	int thread;

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
		thread = p_fh->thread;
		mmapfile(NULL, &mf);
		return thread;
	}
	MMAP_CATCH{
		mmapfile(NULL, &mf);
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

static struct fileheader * findbarticle(struct mmapfile *mf, int filetime, int *num, int mode)
{
	struct fileheader *ptr;
	int total = mf->size / sizeof(struct fileheader);
	if(total == 0)
		return NULL;

	if(*num >= total)
		*num = total;
	if(*num < 0) {
		*num = Search_Bin(mf->ptr, filetime, 0, total - 1);
		if(*num >= 0) {
			ptr = (struct fileheader *)(mf->ptr + *num * sizeof(struct fileheader));
			return ptr;
		}
		return NULL;
	}

	ptr = (struct fileheader *)(mf->ptr + *num * sizeof(struct fileheader));
	int i;
	for(i = (*num); i>=0; i--) {
		if(mode && ptr->filetime < filetime)
			return NULL;

		if(ptr->filetime == filetime) {
			*num = i;
			return ptr;
		}
		ptr--;
	}

	return NULL;
}

static int do_article_post(char *board, char *title, char *filename, char *id,
		char *nickname, char *ip, int sig, int mark, int outgoing, char *realauthor, int thread)
{
	FILE *fp, *fp2;
	char buf3[1024];
	struct fileheader header;
	memset(&header, 0, sizeof(header));
	int t;

	if(strcasecmp(id, "Anonymous") != 0)
		fh_setowner(&header, id, 0);
	else
		fh_setowner(&header, realauthor, 1);

	sprintf(buf3, "boards/%s/", board);

	time_t now_t = time(NULL);
	t = trycreatefile(buf3, "M.%d.A", now_t, 100);
	if(t<0)
		return -1;

	header.filetime = t;
	strsncpy(header.title, title, sizeof(header.title));
	header.accessed |= mark;

	if(outgoing)
		header.accessed |= FH_INND;

	fp = fopen(buf3, "w");
	if(NULL == fp)
		return -1;
	fprintf(fp,
			"发信人: %s (%s), 信区: %s\n标  题: %s\n发信站: %s (%24.24s), %s)\n\n",
			id, nickname, board, title, MY_BBS_NAME, Ctime(now_t),
			outgoing ? "转信(" MY_BBS_DOMAIN : "本站(" MY_BBS_DOMAIN);

	fp2 = fopen(filename, "r");
	if(fp2!=0) {
		while(1) {  // 将 bbstmpfs 中文章主体的内容写到实际文件中
			int retv = fread(buf3, 1, sizeof(buf3), fp2);
			if(retv<=0)
				break;
			fwrite(buf3, 1, retv, fp);
		}

		fclose(fp2);
	}

	// TODO: QMD
	// fprintf(fp, "\n--\n");
	// sig_append

	fprintf(fp, "\033[1;%dm※ 来源:．%s %s [FROM: %.20s]\033[m\n",
			31+rand()%7, MY_BBS_NAME, "API", ip);

	fclose(fp);

	sprintf(buf3, "boards/%s/M.%d.A", board, t);
	header.sizebyte = numbyte(eff_size(buf3));

	if(thread == -1)
		header.thread = header.filetime;
	else
		header.thread = thread;

	sprintf(buf3, "boards/%s/.DIR", board);
	append_record(buf3, &header, sizeof(header));

	//if(outgoing)

	//updatelastpost(board);  //TODO:
	return t;
}
