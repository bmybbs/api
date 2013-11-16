#ifndef __ERROR_CODE_H
#define __ERROR_CODE_H

enum api_error_code {
	API_RT_SUCCESSFUL 	= 0, 		///< 成功调用
	API_RT_NOEMTSESS	= 1,		///< 系统用户已满
	API_RT_CNTLGOTGST	= 2,		///< 不能注销guest用户
	API_RT_NOTOP10FILE	= 3,		///< 没有十大文件
	API_RT_XMLFMTERROR	= 4,		///< 十大、分区热门话题文件格式有误
	API_RT_NOSUCHFILE	= 5,		///< 没有找到对应文件，谨慎使用此错误码
	API_RT_NOCMMNDFILE	= 6,		///< 没有找到美文、通知的文件
	API_RT_NOGDBRDFILE	= 7,		///< 没有用户的收藏夹文件
	API_RT_CNTMAPBRDIR	= 8,		///< 不能 MMAP 版面 .DIR 文件
	API_RT_WRONGPARAM	= 1000,		///< 接口参数错误
	API_RT_WRONGSESS	= 1001,		///< 错误的session
	API_RT_NOTLOGGEDIN	= 1002,		///< 没有登录
	API_RT_FUNCNOTIMPL	= 1003,		///< 功能未实现
	API_RT_NOTEMPLATE	= 1100,		///< 没有模板
	API_RT_NOSUCHUSER 	= 100000, 	///< 没有此用户
	API_RT_SITEFBDIP	= 100001,	///< 站点禁用IP
	API_RT_FORBIDDENIP	= 100002,	///< 用户禁用IP
	API_RT_ERRORPWD		= 100003,	///< 用户密码错误
	API_RT_FBDNUSER		= 100004,	///< 用户没有登录权限
	API_RT_INVSESSID	= 100005,	///< 用户session已失效
	API_RT_NOSUCHBRD	= 110000,	///< 没有此版面
	API_RT_NOBRDRPERM	= 110001,	///< 没有该版面的阅读权限
	API_RT_EMPTYBRD		= 110002,	///< 版面没有文章
	API_RT_NOSUCHATCL	= 120000,	///< 没有这篇文章
	API_RT_ATCLDELETED	= 120001,	///< 文章已被删除
};

#endif
