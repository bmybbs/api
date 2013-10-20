#include "api.h"

int api_error(ONION_FUNC_PROTO_STR, enum api_error_code errcode)
{
	onion_response_set_header(res, "Content-type", "application/json; charset=utf-8");
	onion_response_printf(res, "{\"errcode\":%d}", errcode);
	return OCS_PROCESSED;
}
