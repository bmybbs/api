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
#include "libxml/HTMLparser.h"
#include "libxml/HTMLtree.h"
#include "libxml/xpath.h"
#include "libxml/parser.h"
#include "libxml/tree.h"
#include "libxml/xmlmemory.h"
#include "libxml/xmlsave.h"

#include "bbs.h"
#include "ythtlib.h"
#include "ythtbbs.h"
#include "api_brc.h"

enum article_parse_mode {
	ARTICLE_PARSE_WITH_ANSICOLOR,		///< 将颜色转换为 HTML 样式
	ARTICLE_PARSE_WITHOUT_ANSICOLOR		///< 将 \033 字符转换为 [ESC]
};

enum API_POST_TYPE {
	API_POST_TYPE_POST,		///< 发帖模式
	API_POST_TYPE_REPLY		///< 回帖模式
};

struct bmy_article {
	int type;				///< 类型，1表示主题，0表示一般文章
	char title[80];			///< 标题
	char board[24];			///< 版面id
	char author[16];		///< 作者id
	int filetime;			///< fileheader.filetime，可以理解为文章id
	int thread;				///< 主题id
	int th_num;				///< 参与主题讨论的人数
	unsigned int mark; 		///< 文章标记，fileheader.accessed
	int sequence_num;		///< 文章在版面的序号
};

struct attach_link {
	char link[256];
	unsigned int size;
	struct attach_link *next;
};

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
int insertuseridhash(struct useridhashitem *ptr, int size, char *userid, int num);
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

/**
 * @brief 字符串替换函数
 * @param ori 原始字符串
 * @param old 需要替换的字符串
 * @param new 替换的新字符串
 * @return 替换完成后的字符串
 * @warning ori 字符串应当存在在堆上，同样返回的字符串也位于堆上，使用完成记得 free。
 */
char *string_replace(char *ori, const char *old, const char *new);

/**
 * @brief 读取BMY文章内容，并转换为便于处理或者显示。
 * @param bname 版面名称
 * @param fname 帖子名称
 * @param mode 参见 enum article_parse_mode
 * @param attach_link_list 存放BMY附件链接的链表
 * @return 处理后的字符串，该字符串已转换为 UTF-8 编码。
 * @warning 在使用完成后记得 free。
 */
char *parse_article(const char *bname, const char *fname, int mode, struct attach_link **attach_link_list);

/**
 * @brief 将文章中的附件链接单独存放在 attach_link 链表中。
 * @param attach_link_list
 * @param link 附件链接
 * @param size 附件大小
 */
void add_attach_link(struct attach_link **attach_link_list, const char *link, const unsigned int size);

/**
 * @brief 释放附件链表
 * @param attach_link_list
 */
void free_attach_link_list(struct attach_link *attach_link_list);

/**
 * @brief 将字符串写入指定的文件中。
 * 该方法来自 nju09。
 * @param filename 文件名
 * @param buf 字符串
 * @return 成功返回 0，失败返回 -1。
 */
int f_write(char *filename, char *buf);

/**
 * @brief 将字符串追加到指定的文件中。
 * 该方法来自 nju09。
 * @param filename 文件名
 * @param buf 字符串
 * @return 成功返回 0，失败返回 -1。
 */
int f_append(char *filename, char *buf);

/**
 * @brief 向指定的文件中追加记录。
 * 该方法来自 nju09。
 * @param filename 文件名
 * @param record 需要存放的记录
 * @param size 需要存放的记录长度
 * @return
 */
int append_record(char *filename, void *record, int size);

/**
 * @brief 计算某个id的站内信封数。
 * 该方法来自 nju09/bbsfoot.c int mails()。该函数不包含用户id有效性校验，需要在逻辑
 * 中预先校验权限。
 * @param id 用户id
 * @param unread 未读封数
 * @return 总站内信封数
 */
int mail_count(char *id, int *unread);

/**
 * @brief 计算用户等级
 * 该方法用于输出 utf8 编码的字符串，需要与 libythtbbs 保持一致。
 * @param exp 输入经验值
 * @return
 */
char * calc_exp_str_utf8(int exp);

/**
 * @brief 评价用户经验值
 * 该方法用于输出 utf8 编码的字符串，需要与 libythtbbs 保持一致。
 * @param perf
 * @return
 */
char * calc_perf_str_utf8(int perf);

/**
 * @brief 实际处理发文的函数。
 * 该函数来自 nju09。
 * @param board 版面名称
 * @param title 文章标题, utf8 编码
 * @param filename 位于 bbstmpfs 中的文章内容
 * @param id 用于显示的作者 id
 * @param nickname 作者昵称
 * @param ip 来自 ip
 * @param sig 选用的签名档数字
 * @param mark fileheader 的标记
 * @param outgoing 是否转信
 * @param realauthor 实际的作者 id
 * @param thread 主题编号
 * @return 返回文件名中实际使用的时间戳
 */
int do_article_post(char *board, char *title, char *filename, char *id,
					char *nickname, char *ip, int sig, int mark,
					int outgoing, char *realauthor, int thread);

/**
 * @brief 实际处理发站内信的函数。
 * 该函数参考 nju09。其中 filename 指向的文件需为 gbk 编码。本函数仅用于对站内用户发信。
 * @warning 需要提前做好用户是否可用的验证。
 * @param to_userid 指向的用户
 * @param title 站内信标题，utf8 编码
 * @param filename 位于 bbstmpfs 中的文章内容
 * @param id 用于显示的作者 id
 * @param nickname 作者昵称
 * @param ip 来自 ip
 * @param sig 选用的签名档数字
 * @param mark fileheader 的标记
 * @return 返回文件名中实际使用的时间戳
 */
int do_mail_post(char *to_userid, char *title, char *filename, char *id,
				 char *nickname, char *ip, int sig, int mark);

/**
 * @brief 将 ansi 颜色控制转换成 HTML 标记
 * 该方法来自 theZiz/aha。略作修改。
 * @param in_stream 读入的流
 * @param out_stream 输出的流
 */
void aha_convert(FILE *in_stream, FILE *out_stream);
#endif
