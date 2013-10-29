#ifndef __BMYBBS_APILIB_H
#define __BMYBBS_APILIB_H

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ght_hash_table.h>
#include <json/json.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
#include <libxml/xpath.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlsave.h>

#include "bbs.h"
#include "ythtlib.h"
#include "ythtbbs.h"

typedef char* api_template_t;
api_template_t api_template_create(const char * filename);
void api_template_set(api_template_t *tpl, const char *key, char *fmt, ...);
void api_template_free(api_template_t tpl);

struct UTMPFILE   *shm_utmp;
struct BCACHE     *shm_bcache;
struct UCACHE     *shm_ucache;
struct UCACHEHASH *shm_uidhash;
struct UINDEX     *shm_uindex;
int shm_init();

extern char *ummap_ptr;
extern int ummap_size;
int ummap();


int finduseridhash(struct useridhashitem *ptr, int size, const char *userid);
int getusernum(const char *id);
struct userec * getuser(const char *id);
char * getuserlevelname(unsigned userlevel);
int save_user_data(struct userec *x);

/**
 * @brief 从 sessid 中获取 utmp 的索引值
 * @param sessid 前三位为索引的sessionid
 * @return utmp 的索引值，注意是从0开始。
 */
int get_user_utmp_index(const char *sessid);

/**
 * @brief 计算uid为uid的用户当前在登录的个数
 * @param uid uid，从1开始索引
 * @return
 */
int count_uindex(int uid);

/**
 * @brief 检查用户 session 是否有效
 * @param x
 * @param sessid
 * @param appkey
 * @return api_error_code
 */
int check_user_session(struct userec *x, const char *sessid, const char *appkey);

int setbmhat(struct boardmanager *bm, int *online);
int setbmstatus(struct userec *ue, int online);

#endif
