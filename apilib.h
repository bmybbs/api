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


int finduseridhash(struct useridhashitem *ptr, int size, char *userid);
int getusernum(const char *id);
struct userec * getuser(const char *id);
char * getuserlevelname(unsigned userlevel);
int save_user_data(struct userec *x);

#endif
