#include "api.h"

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
	struct mmapfile mf = {ptr:NULL};

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
