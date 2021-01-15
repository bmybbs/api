#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include <json-c/json.h>
#include <onion/shortcuts.h>

#include "config.h"
#include "ytht/fileop.h"
#include "ythtbbs/user.h"

#include "api.h"
#include "apilib.h"

static int api_attach_show_mail(ONION_FUNC_PROTO_STR);

static void output_binary_attach(onion_response *res, const char *filename, const char *attachname, int attachpos);

static char * get_mime_type(const char *name);

int api_attach_show(ONION_FUNC_PROTO_STR)
{
	const char *type = onion_request_get_query(req, "type");
	if(!strcasecmp(type, "mail"))
		return api_attach_show_mail(p, req, res);
	else
		return api_error(p, req, res, API_RT_WRONGPARAM);
}

int api_attach_list(ONION_FUNC_PROTO_STR)
{
	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");

	if(!userid || !sessid || !appkey)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	int r = check_user_session(ue, sessid, appkey);
	if(r != API_RT_SUCCESSFUL) {
		free(ue);
		return api_error(p, req, res, r);
	}

	char userattachpath[256];
	snprintf(userattachpath, sizeof(userattachpath), PATHUSERATTACH "/%s", ue->userid);
	mkdir(userattachpath, 0760);
	free(ue);

	DIR *pdir;
	struct dirent *pdent;
	char fname[1024];

	pdir = opendir(userattachpath);
	if(!pdir)
		return api_error(p, req, res, API_RT_NOSUCHFILE);

	struct json_object * obj = json_tokener_parse("{\"errcode\":0, \"attach_array\":[]}");
	struct json_object * aa = json_object_object_get(obj, "attach_array");

	while((pdent = readdir(pdir))) {
		if(!strcmp(pdent->d_name, "..") || !strcmp(pdent->d_name, "."))
			continue;

		if(strlen(pdent->d_name) + strlen(userattachpath) >= sizeof(fname) - 2) {
			break;
		}

		sprintf(fname, "%s/%s", userattachpath, pdent->d_name);

		struct json_object * attach_obj = json_object_new_object();
		json_object_object_add(attach_obj, "file_name", json_object_new_string(pdent->d_name));
		json_object_object_add(attach_obj, "size", json_object_new_int(file_size_s(fname)));
		json_object_array_add(aa, attach_obj);
	}

	closedir(pdir);
	api_set_json_header(res);
	onion_response_write0(res, json_object_to_json_string(obj));
	json_object_put(obj);

	return OCS_PROCESSED;
}

int api_attach_upload(ONION_FUNC_PROTO_STR)
{
	char filename_buf[512];
	if(!(onion_request_get_flags(req) & OR_POST))
		return api_error(p, req, res, API_RT_FUNCNOTIMPL);

	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");

	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	int r = check_user_session(ue, sessid, appkey);
	if(r != API_RT_SUCCESSFUL) {
		free(ue);
		return api_error(p, req, res, r);
	}

	char userattachpath[256], finalname[1024];
	snprintf(userattachpath, sizeof(userattachpath), PATHUSERATTACH "/%s", ue->userid);
	mkdir(userattachpath, 0760);
	free(ue);

	const char * name=onion_request_get_post(req,"file");
	const char * filename=onion_request_get_file(req,"file");
	const char * length_str = onion_request_get_header(req, "Content-Length");

	if(!name || !filename || !length_str)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	if(strlen(userattachpath) + strlen(name) > 1022)	// 1024 - '\0' - '/'
		return api_error(p, req, res, API_RT_WRONGPARAM);

	snprintf(filename_buf, sizeof(filename_buf), "%s/%s", userattachpath, name);
	const char * ext_name = name + strlen(name);
	int upload_size = atoi(length_str);
	if (upload_size <= 0 || upload_size > 5000000)
		return api_error(p, req, res, API_RT_ATTTOOBIG);
	if (!strcasecmp(ext_name - 4, ".gif") || !strcasecmp(ext_name - 4, ".jpg") ||
		!strcasecmp(ext_name - 4, ".bmp") || !strcasecmp(ext_name - 4, ".png") ||
		!strcasecmp(ext_name - 4, ".jpeg")) {
		if (upload_size > MAXPICSIZE) {
			return api_error(p, req, res, API_RT_ATTTOOBIG);
		}
	}

	int current_size = 0;
	DIR *pdir;
	struct dirent *pdent;
	char fname[1024];

	pdir = opendir(userattachpath);
	if(!pdir)
		return api_error(p, req, res, API_RT_NOSUCHFILE);

	while((pdent = readdir(pdir))) {
		if(!strcmp(pdent->d_name, "..") || !strcmp(pdent->d_name, "."))
			continue;

		if(strlen(pdent->d_name) + strlen(userattachpath) >= sizeof(fname) - 2) {
			closedir(pdir);
			return api_error(p, req, res, API_RT_ATTINNERR);
		}

		sprintf(fname, "%s/%s", userattachpath, pdent->d_name);
		current_size += file_size_s(fname);
	}

	closedir(pdir);

	if(current_size > MAXATTACHSIZE || current_size + upload_size > MAXATTACHSIZE)
		return api_error(p, req, res, API_RT_ATTNOSPACE);

	onion_shortcut_rename(filename, finalname);

	return api_error(p, req, res, API_RT_SUCCESSFUL);
}

static int api_attach_show_mail(ONION_FUNC_PROTO_STR)
{
	const char * userid = onion_request_get_query(req, "userid");
	const char * sessid = onion_request_get_query(req, "sessid");
	const char * appkey = onion_request_get_query(req, "appkey");
	const char * str_mid = onion_request_get_query(req, "mid");
	const char * str_pos = onion_request_get_query(req, "pos");
	const char * attname = onion_request_get_query(req, "attname");

	if(!userid || !sessid || !appkey || !str_mid || !str_pos || !attname)
		return api_error(p, req, res, API_RT_WRONGPARAM);

	struct userec *ue = getuser(userid);
	if(ue == 0)
		return api_error(p, req, res, API_RT_NOSUCHUSER);

	int r = check_user_session(ue, sessid, appkey);
	if(r != API_RT_SUCCESSFUL) {
		free(ue);
		return api_error(p, req, res, r);
	}

	char mailfilename[STRLEN];
	sprintf(mailfilename, MY_BBS_HOME "/mail/%c/%s/M.%s.A", mytoupper(ue->userid[0]), ue->userid, str_mid);


	output_binary_attach(res, mailfilename, attname, atoi(str_pos));

	free(ue);
	return OCS_PROCESSED;
}

static void output_binary_attach(onion_response *res, const char *filename, const char *attachname, int attachpos)
{
	struct mmapfile mf = {.ptr = NULL};

	if(mmapfile(filename, &mf) < 0) {
		api_error(NULL, NULL, res, API_RT_MAILATTERR);
		return ;
	}

	if(attachpos >= mf.size-4 || attachpos < 1) {
		mmapfile(NULL, &mf);
		api_error(NULL, NULL, res, API_RT_MAILATTERR);
		return ;
	}

	if(mf.ptr[attachpos-1] != 0) {
		mmapfile(NULL, &mf);
		api_error(NULL, NULL, res, API_RT_MAILATTERR);
		return ;
	}

	/* attachpos 的说明
	* 例如原文件为：
	* beginbinaryattach test.txt\n\0\0\0\0\024....
	* 省略号为 test.txt 的正文
	* 此处 attachpos 指向的位置为 \n
	*/
	unsigned int size = ntohl(*(unsigned int *)(mf.ptr + attachpos));
	char * body = (char *)malloc(size);

	if(body==NULL) {
		mmapfile(NULL, &mf);
		api_error(NULL, NULL, res, API_RT_NOTENGMEM);
		return ;
	}

	onion_response_set_header(res, "Content-Type", get_mime_type(filename));
	onion_response_write(res, mf.ptr+attachpos+4, size);

	mmapfile(NULL, &mf);
	free(body);
}

static char * get_mime_type(const char *name)
{
	char * dot = strrchr(name, '.');

	if(dot == NULL)
		return "text/plain";

	if (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0)
		return "text/html";
	if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0)
		return "image/jpeg";
	if (strcasecmp(dot, ".gif") == 0)
		return "image/gif";
	if (strcasecmp(dot, ".png") == 0)
		return "image/png";
	if (strcasecmp(dot, ".pcx") == 0)
		return "image/pcx";
	if (strcasecmp(dot, ".css") == 0)
		return "text/css";
	if (strcasecmp(dot, ".au") == 0)
		return "audio/basic";
	if (strcasecmp(dot, ".wav") == 0)
		return "audio/wav";
	if (strcasecmp(dot, ".avi") == 0)
		return "video/x-msvideo";
	if (strcasecmp(dot, ".mov") == 0 || strcasecmp(dot, ".qt") == 0)
		return "video/quicktime";
	if (strcasecmp(dot, ".mpeg") == 0 || strcasecmp(dot, ".mpe") == 0)
		return "video/mpeg";
	if (strcasecmp(dot, ".vrml") == 0 || strcasecmp(dot, ".wrl") == 0)
		return "model/vrml";
	if (strcasecmp(dot, ".midi") == 0 || strcasecmp(dot, ".mid") == 0)
		return "audio/midi";
	if (strcasecmp(dot, ".mp3") == 0)
		return "audio/mpeg";
	if (strcasecmp(dot, ".pac") == 0)
		return "application/x-ns-proxy-autoconfig";
	if (strcasecmp(dot, ".txt") == 0)
		return "text/plain";
	if (strcasecmp(dot, ".xht") == 0 || strcasecmp(dot, ".xhtml") == 0)
		return "application/xhtml+xml";
	if (strcasecmp(dot, ".xml") == 0)
		return "text/xml";
	return "application/octet-stream";
}
