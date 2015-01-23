#include "api.h"

onion *o=NULL;

static void shutdown_server(int _)
{
	if (o)
		onion_listen_stop(o);
}

int main(int argc, char *argv[])
{
	seteuid(BBSUID);
	setuid(BBSUID);
	setgid(BBSGID);

	chdir(MY_BBS_HOME);

	if(shm_init()<0)
		return -1;
	if(ummap()<0)
		return -1;

	signal(SIGINT, shutdown_server);
	signal(SIGTERM, shutdown_server);

	o=onion_new(O_POOL);
	onion_set_max_threads(o, 32);

	onion_set_timeout(o, 5000);
	onion_set_hostname(o, "127.0.0.1");
	onion_set_port(o, "8081");

	onion_url *urls=onion_root_url(o);
	onion_url_add(urls, "", api_error);

	onion_url_add(urls, "^user/query$", api_user_query);
	onion_url_add(urls, "^user/login$", api_user_login);
	onion_url_add(urls, "^user/logout$", api_user_logout);
	onion_url_add(urls, "^user/checksession$", api_user_check_session);
	onion_url_add(urls, "^user/register$", api_user_register);
	onion_url_add(urls, "^user/articlequery", api_user_articlequery);
	onion_url_add(urls, "^article/list$", api_article_list);
	onion_url_add(urls, "^article/getHTMLContent$", api_article_getHTMLContent);
	onion_url_add(urls, "^article/getRAWContent$", api_article_getRAWContent);
	onion_url_add(urls, "^article/post$", api_article_post);
	onion_url_add(urls, "^article/reply$", api_article_reply);
	onion_url_add(urls, "^board/list$", api_board_list);
	onion_url_add(urls, "^board/info$", api_board_info);
	onion_url_add(urls, "^meta/loginpics", api_meta_loginpics);
	onion_url_add(urls, "^mail/list$", api_mail_list);
	onion_url_add(urls, "^mail/getHTMLContent$", api_mail_getHTMLContent);
	onion_url_add(urls, "^mail/getRAWContent$", api_mail_getRAWContent);
	onion_url_add(urls, "^mail/post$", api_mail_send);
	onion_url_add(urls, "^mail/reply$", api_mail_reply);
	onion_url_add(urls, "^attach/show$", api_attach_show);

	onion_listen(o);

	onion_free(o);
	return 0;
}
