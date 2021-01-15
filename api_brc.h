/**
 * @file	api_brc.h
 * @brief	libythtbbs/boardrc.h 函数的封装，用来处理版面阅读记录。
 * @details	这些方法源自 nju09，但是需要应用于多线程的环境，因此和 nju09 版本
 * 			不是完全相同，使用中请注意接口的说明。
 * @author	IronBlood
 * @date	2013-11-01
 */

#ifndef BMYBBS_API_BRC_H
#define BMYBBS_API_BRC_H
#include <stddef.h>
#include "ythtbbs/cache.h"

int brc_initial(char *userid, char *boardname,struct allbrc *allbrc, char *allbrcuser, size_t allbrcuser_size, const char *fromhost, struct user_info *u_info, struct onebrc **pbrc, struct onebrc *brc);
#endif
