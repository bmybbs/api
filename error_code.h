#ifndef __ERROR_CODE_H
#define __ERROR_CODE_H

enum api_error_code {
	API_RT_SUCCESSFUL 	= 0, 		///< 成功调用
	API_RT_NOEMTSESS	= 1,		///< 系统用户已满
	API_RT_CNTLGOTGST	= 2,		///< 不能注销guest用户
	API_RT_NOTOP10FILE  = 3,		///< 没有十大文件
	API_RT_NOSUCHFILE   = 30,		///< 没有找到对应文件
	API_RT_NOCOMMENDFILE = 31,		///< 没有找到美文、通知的文件
	API_RT_WRONG_BOARD_NAME = 320,		///< 错误的版面名
	API_RT_FAIL_TO_GET_BOARD = 321,		///< 版面名正确，但是未能正确读曲文件
	API_RT_XMLFMTERROR  = 4,		///< 十大、分区热门话题文件格式有误
	API_RT_WRONGPARAM	= 1000,		///< 接口参数错误
	API_RT_WRONGSESS	= 1001,		///< 错误的session
	API_RT_NOTEMPLATE	= 1100,		///< 没有模板
	API_RT_NOSUCHUSER 	= 100000, 	///< 没有此用户
	API_RT_SITEFBDIP	= 100001,	///< 站点禁用IP
	API_RT_FORBIDDENIP	= 100002,	///< 用户禁用IP
	API_RT_ERRORPWD		= 100003,	///< 用户密码错误
	API_RT_FBDNUSER		= 100004,	///< 用户没有登录权限
	API_RT_INVSESSID	= 100005,	///< 用户session已失效
};

#endif
