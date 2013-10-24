#ifndef __ERROR_CODE_H
#define __ERROR_CODE_H

enum api_error_code {
	API_RT_SUCCESSFUL 	= 0, 		///< 成功调用
	API_RT_NOEMTSESS	= 1,		///< 系统用户已满
	API_RT_WRONGPARAM	= 1000,		///< 接口参数错误
	API_RT_NOTEMPLATE	= 1100,		///< 没有模板
	API_RT_NOSUCHUSER 	= 100000, 	///< 没有此用户
	API_RT_SITEFBDIP	= 100001,	///< 站点禁用IP
	API_RT_FORBIDDENIP	= 100002,	///< 用户禁用IP
	API_RT_ERRORPWD		= 100003,	///< 用户密码错误
	API_RT_FBDNUSER		= 100004,	///< 用户没有登录权限
};

#endif
