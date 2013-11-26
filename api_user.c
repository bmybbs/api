#include "api.h"

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
	api_template_set(&tpl, "UserID", ue->userid);
	api_template_set(&tpl, "SessionID", "%c%c%c%s",
			(utmp_index-1) / 26 / 26 + 'A',
			(utmp_index-1) / 26 % 26 + 'A',
			(utmp_index-1) % 26 + 'A',
			shm_utmp->uinfo[utmp_index-1].sessionid);
	api_template_set(&tpl, "Token", shm_utmp->uinfo[utmp_index-1].token);

	api_set_json_header(res);
	onion_response_write0(res, tpl);

	api_template_free(tpl);
	free(ue);
	return OCS_PROCESSED;
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

	api_set_json_header(res);
	onion_response_write0(res, tpl);

	api_template_free(tpl);
	free(ue);

	return OCS_PROCESSED;
}

int api_user_logout(ONION_FUNC_PROTO_STR)
{
	const onion_dict *param_dict = onion_request_get_query_dict(req);
	const char * userid = onion_dict_get(param_dict, "userid");
	const char * sessid = onion_dict_get(param_dict, "sessid");
	const char * appkey = onion_dict_get(param_dict, "appkey");
	const char * fromhost = onion_request_get_client_description(req);
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
