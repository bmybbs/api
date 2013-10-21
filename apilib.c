/*
 * apilib.c
 *
 *  Created on: 2012-10-29
 *      Author: shenyang
 */

#include "apilib.h"
char *ummap_ptr = NULL;
int ummap_size = 0;

/** 再应用程序启动的时候初始化共享内存
 *
 * @return <ul><li>0:成功</li><li>-1:失败</li></ul>
 */
int shm_init()
{
	shm_utmp    = (struct UTMPFILE*) get_old_shm(UTMP_SHMKEY, sizeof(struct UTMPFILE));
	shm_bcache  = (struct BCACHE*) get_old_shm(BCACHE_SHMKEY, sizeof(struct BCACHE));
	shm_ucache  = (struct UCACHE*) get_old_shm(UCACHE_SHMKEY, sizeof(struct UCACHE));
	shm_uidhash = (struct UCACHEHASH*) get_old_shm(UCACHE_HASH_SHMKEY, sizeof(struct UCACHEHASH));
	shm_uindex  = (struct UINDEX*) get_old_shm(UINDEX_SHMKEY, sizeof(struct UINDEX));
	if( shm_utmp == 0 ||
		shm_bcache == 0 ||
		shm_ucache == 0 ||
		shm_uidhash == 0 ||
		shm_uindex == 0 )
		return -1; // shm error
	else
		return 0;
}

/** 映射 .PASSWDS 文件到内存
 * 设置 ummap_ptr 地址为文件映射的地址，并且
 * 设置 ummap_size 为文件大小。
 * 该方法来自于 nju09/BBSLIB.c 。
 * @warning 尚未确定该方法是否线程安全。
 * @return <ul><li>0: 成功</li><li>-1: 失败</li></ul>
 */
int ummap()
{
	int fd;
	struct stat st;
	if(ummap_ptr)
		munmap(ummap_ptr, ummap_size);
	ummap_ptr = NULL;
	ummap_size = 0;
	fd = open(".PASSWDS", O_RDONLY);
	if(fd<0)
		return -1;
	if(fstat(fd, &st)<0 || !S_ISREG(st.st_mode) || st.st_size<=0) {
		close(fd);
		return -1;
	}
	ummap_ptr = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);

	if(ummap_ptr == MAP_FAILED)
		return -1;

	ummap_size = st.st_size;
	return 0;
}

/** 从共享内存中寻找用户
 * Hash userid and get index in PASSWDS file.
 * @warning remember to free userec address!
 * @param id
 * @return
 * @see getusernum
 * @see finduseridhash
 */
struct userec * getuser(const char *id)
{
	struct userec *user = malloc(sizeof(struct userec));
	int uid;
	uid = getusernum(id);
	if(uid<0)
		return NULL;
	if((uid+1) * sizeof(struct userec) > ummap_size)
		ummap(); // 重新 mmap PASSWDS 文件到内存
	if(!ummap_ptr)
		return 0;
	memcpy(user, ummap_ptr + sizeof(*user) * uid, sizeof(*user));
	return user;
}

int getusernum(const char *id)
{
	int i;
	if(id[0] == 0 || strchr(id, '.'))
		return -1;

	i = finduseridhash(shm_uidhash->uhi, UCACHE_HASH_SIZE, id) - 1;
	if (i>=0 && !strcasecmp(shm_ucache->userid[i], id))
		return i;    // check user in shm_ucache
	for (i=0; i<MAXUSERS; i++) {
		if (!strcasecmp(shm_ucache->userid[i], id))
			return i;  // 遍历 shm_ucache 找到真实的索引
	}
	return -1;
}

/** hash user id
 * Only have 25 * 26 + 25 = 675 different hash values.
 * From nju09/BBSLIB.c
 * @param id
 * @return
 */
int useridhash(char *id)
{
	int n1 = 0;
	int n2 = 0;
	while(*id) {
		n1 += ((unsigned char) toupper(*id)) % 26;
		id ++;
		if(!*id)
			break;
		n2 += ((unsigned char) toupper(*id)) % 26;
		id ++;
	}
	n1 %= 26;
	n2 %= 26;
	return n1 *26 + n2;
}

/** find user from shm_uidhash
 *
 * @param ptr pointer to UCACHEHASH
 * @param size UCACHE_HASH_SIZE
 * @param userid
 * @return
 */
int finduseridhash(struct useridhashitem *ptr, int size, char *userid)
{
	int h, s, i, j;
	h = useridhash(userid);
	s = size / 26 / 26;
	i = h * s;
	for(j=0; j<s*5; j++) {
		if(!strcasecmp(ptr[i].userid, userid))
			return ptr[i].num;
		i++;
		if (i >= size)
			i %= size;
	}

	return -1;
}

/** 依据用户权限位获取本站职位名称
 *
 * @param userlevel
 * @return
 */
char * getuserlevelname(unsigned userlevel)
{
	if((userlevel & PERM_SYSOP) && (userlevel & PERM_ARBITRATE))
		return "本站顾问团";
	else if(userlevel & PERM_SYSOP)
		return "现任站长";
	else if(userlevel & PERM_OBOARDS)
		return "实习站长";
	else if(userlevel & PERM_ARBITRATE)
		return "现任纪委";
	else if(userlevel & PERM_SPECIAL4)
		return "区长";
	else if(userlevel & PERM_WELCOME)
		return "系统美工";
	else if(userlevel & PERM_SPECIAL7) {
		if((userlevel & PERM_SPECIAL1) && (userlevel & PERM_CLOAK))
			return "离任程序员";
		else
			return "程序组成员";
	} else if(userlevel & PERM_ACCOUNTS)
		return "帐号管理员";
	else
		return NULL;
}

/** 保存用户数据到 passwd 文件中
 * @warning 还不是线程安全的。
 * @param x
 * @return
 */
int save_user_data(struct userec *x)
{
	int n, fd;
	n = getusernum(x->userid);
	if(n < 0 || n > 1000000)
		return 0;
	fd = open(".PASSWDS", O_WRONLY);
	if(fd < 0)
		return 0;
	if(lseek(fd, n*sizeof(struct userec), SEEK_SET) < 0) {
		close(fd);
		return 0;
	}
	write(fd, x, sizeof(struct userec));
	close(fd);
	return 1;
}
