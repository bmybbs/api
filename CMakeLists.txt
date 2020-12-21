add_executable(bmyapi
	main.c
	api_error.c
	api_template.c
	api_user.c
	apilib.c
	api_article.c
	api_board.c
	api_brc.c
	api_meta.c
	api_attach.c
	api_mail.c
	api_notification.c
	api_subscription.c
)

target_link_libraries(bmyapi ythtbbs ytht onion json-c hiredis)
