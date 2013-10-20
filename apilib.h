#ifndef __BMYBBS_APILIB_H
#define __BMYBBS_APILIB_H

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ght_hash_table.h>

#include "bbs.h"
#include "ythtlib.h"
#include "ythtbbs.h"

struct UTMPFILE   *shm_utmp;
struct BCACHE     *shm_bcache;
struct UCACHE     *shm_ucache;
struct UCACHEHASH *shm_uidhash;
struct UINDEX     *shm_uindex;
int shm_init();

char *ummap_ptr = NULL;
int ummap_size = 0;
int ummap();


int finduseridhash(struct useridhashitem *ptr, int size, char *userid);
int getusernum(char *id);
struct userec * getuser(char *id);
char * getuserlevelname(unsigned userlevel);
int save_user_data(struct userec *x);

#endif
