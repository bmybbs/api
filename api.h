#ifndef __BMYBBS_API_H
#define __BMYBBS_API_H
#include "apilib.h"
#include "error_code.h"

#include <onion/onion.h>
#include <onion/dict.h>
#include <onion/log.h>
#include <signal.h>
#include <netdb.h>

onion *o=NULL;

#define ONION_FUNC_PROTO_STR void *p, onion_request *req, onion_response *res

int api_error(ONION_FUNC_PROTO_STR, enum api_error_code errcode);

int api_user_login(ONION_FUNC_PROTO_STR);
int api_user_query(ONION_FUNC_PROTO_STR);
int api_user_logout(ONION_FUNC_PROTO_STR);
int api_user_check_session(ONION_FUNC_PROTO_STR);

#endif
