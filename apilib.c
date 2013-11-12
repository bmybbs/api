/*
 * apilib.c
 *
 *  Created on: 2012-10-29
 *      Author: shenyang
 */

#include "apilib.h"
#include "error_code.h"
char *ummap_ptr = NULL;
int ummap_size = 0;

typedef struct selem *pelem;
typedef struct selem {
	unsigned char digit[8];
	unsigned char digitcount;
	pelem next;
} telem;

/**
 * @brief 解析 ansi 控制符
 * 该方法来自 theZiz/aha。
 * @param s
 * @return
 */
pelem parseInsert(char* s);

/**
 * @brief 删除 ansi 解析
 * 该方法来自 theZiz/aha。
 * @param elem
 */
void deleteParse(pelem elem);

/**
 * @ 获取下一个字符
 * 该方法来自 theZiz/aha。
 * @param fp
 * @param future
 * @param future_char
 * @return
 */
int getNextChar(register FILE* fp, int *future, int *future_char);

/**
 * @brief 将 ansi 颜色控制转换成 HTML 标记
 * 该方法来自 theZiz/aha。略作修改。
 * @param in_stream 读入的流
 * @param out_stream 输出的流
 */
void aha_convert(FILE *in_stream, FILE *out_stream);

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
	int uid;
	uid = getusernum(id);
	if(uid<0)
		return NULL;
	if((uid+1) * sizeof(struct userec) > ummap_size)
		ummap(); // 重新 mmap PASSWDS 文件到内存
	if(!ummap_ptr)
		return 0;

	struct userec *user = malloc(sizeof(struct userec));
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
int useridhash(const char *id)
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
int finduseridhash(struct useridhashitem *ptr, int size, const char *userid)
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
 * @warning 线程安全有待检查。
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
	flock(fd, LOCK_EX);
	if(lseek(fd, n*sizeof(struct userec), SEEK_SET) < 0) {
		close(fd);
		return 0;
	}
	write(fd, x, sizeof(struct userec));
	flock(fd, LOCK_UN);
	close(fd);
	return 1;
}

int setbmstatus(struct userec *ue, int online)
{
	char path[256];
	sethomefile(path, ue->userid, "mboard");

	bmfilesync(ue);

	new_apply_record(path, sizeof(struct boardmanager), (void *) setbmhat, &online);
	return 0;
}

int setbmhat(struct boardmanager *bm, int *online)
{
	/*
	if(strcmp(shm_bcache->bcache[bm->bid].header.filename, bm->board)) {
		errlog("error board name %s, %s. user %d",
			   shm_bcache->bcache[bm->bid].header.filename,
			   bm->board, bm->bid);
		return -1;
	}
	if(*online) {
		shm_bcache->bcache[bm->bid].bmonline |= (1 << bm->bmpos);
		if(u)
	}*/

	//TODO
	return 0;
}

int get_user_utmp_index(const char *sessid)
{
	return (sessid[0] - 'A') * 26 * 26
			+(sessid[1] - 'A') * 26
			+(sessid[2] - 'A');
}

int count_uindex(int uid)
{
	int i, utmp_index, count=0;
	struct user_info *ui;
	if(uid <= 0 || uid > MAXUSERS)
		return 0;

	for (i=0; i<6; i++) {
		utmp_index = shm_uindex->user[uid-1][i];
		if(utmp_index<=0)
			continue;
		ui = &(shm_utmp->uinfo[utmp_index-1]);
		if(!ui->active || ui->pid==0 || ui->uid != uid)
			continue;

		count++;
	}

	return count;
}

int check_user_session(struct userec *x, const char *sessid, const char *appkey)
{
	if(!x || !sessid || !appkey)
		return API_RT_WRONGSESS;

	int uent_index = get_user_utmp_index(sessid);
	char ssid[30];
	strncpy(ssid, sessid+3, 30);

	struct user_info *ui = &(shm_utmp->uinfo[uent_index]);

#ifdef APIDEBUG
	int uid = getusernum(x->userid);
	int i,y;
	for(i=0; i<6; i++) {
		y=shm_uindex->user[uid][i];
		if(y!=0) {
			y--;
			printf("%d\t%d\t%c%c%c\t%s\n", i, y, y/26/26+65, y/26%26+65, y%26+65, shm_utmp->uinfo[y].sessionid);
		}
	}
#endif

	if(ui->pid == APPPID
			&& strcasecmp(ui->userid, x->userid)==0
			&& strcasecmp(ui->sessionid, ssid)==0
			&& strcasecmp(ui->appkey, appkey)==0)
		return API_RT_SUCCESSFUL;
	else
		return API_RT_WRONGSESS;
}

char *string_replace(char *ori, const char *old, const char *new)
{
	int tmp_string_length = strlen(ori) + strlen(new) - strlen(old) + 1;

	char *ch;
	ch = strstr(ori, old);

	if(!ch)
		return ori;

	char *tmp_string = (char *)malloc(tmp_string_length);
	if(tmp_string == NULL) return ori;

	memset(tmp_string, 0, tmp_string_length);
	strncpy(tmp_string, ori, ch - ori);
	*(tmp_string + (ch - ori)) = 0;
	sprintf(tmp_string + (ch - ori), "%s%s", new, ch+strlen(old));
	*(tmp_string + tmp_string_length - 1) = 0;

	free(ori);
	ori = tmp_string;

	return ori;
}

void add_attach_link(struct attach_link **attach_link_list, const char *link, const unsigned int size)
{
	struct attach_link *a = (struct attach_link *)malloc(sizeof(struct attach_link));
	memset(a, 0, sizeof(*a));
	strncpy(a->link, link, 256);
	a->size = size;

	if(!(*attach_link_list))
		*attach_link_list = a;
	else
		(*attach_link_list)->next = a;
}

void free_attach_link_list(struct attach_link *attach_link_list)
{
	struct attach_link *cur=NULL, *p=NULL;
	cur = attach_link_list;
	while(cur) {
		p = cur->next;
		free(cur);
		cur = p;
	}
}

pelem parseInsert(char* s)
{
	pelem firstelem=NULL;
	pelem momelem=NULL;
	unsigned char digit[8];
	unsigned char digitcount=0;
	unsigned char a;
	int pos=0;
	for (pos=0;pos<1024;pos++)
	{
		if (s[pos]=='[')
			continue;
		if (s[pos]==';' || s[pos]==0)
		{
			if (digitcount<=0)
			{
				digit[0]=0;
				digitcount=1;
			}

			pelem newelem=(pelem)malloc(sizeof(telem));
			for (a=0;a<8;a++)
				newelem->digit[a]=digit[a];
			newelem->digitcount=digitcount;
			newelem->next=NULL;
			if (momelem==NULL)
				firstelem=newelem;
			else
				momelem->next=newelem;
			momelem=newelem;
			digitcount=0;
			memset(digit,0,8);
			if (s[pos]==0)
				break;
		}
		else
		if (digitcount<8)
		{
			digit[digitcount]=s[pos]-'0';
			digitcount++;
		}
	}
	return firstelem;
}

void deleteParse(pelem elem)
{
	while (elem!=NULL)
	{
		pelem temp=elem->next;
		free(elem);
		elem=temp;
	}
}

int getNextChar(register FILE* fp, int *future, int *future_char)
{
	int c;
	if (*future)
	{
		*future=0;
		return *future_char;
	}
	if ((c = fgetc(fp)) != EOF)
		return c;
	return -1; // error
}

char *parse_article(const char *bname, const char *fname, int mode, struct attach_link **attach_link_list)
{
	if(!bname || !fname)
		return NULL;

	if(mode!=ARTICLE_PARSE_WITHOUT_ANSICOLOR && mode!=ARTICLE_PARSE_WITH_ANSICOLOR)
		return NULL;

	char article_filename[256];
	sprintf(article_filename, "boards/%s/%s", bname, fname);
	FILE *article_stream = fopen(article_filename, "r");
	if(!article_stream)
		return NULL;

	FILE *mem_stream, *html_stream;
	char buf[512], link[256], *tmp_buf, *mem_buf, *html_buf, *attach_filename;
	size_t mem_buf_len, html_buf_len, attach_file_size;
	int attach_no;

	mem_stream = open_memstream(&mem_buf, &mem_buf_len);
	fseek(article_stream, 0, SEEK_SET);

	while(1) {
		if(fgets(buf, 500, article_stream) == 0)
			break;

		// 附件处理
		if(!strncmp(buf, "begin 644", 10)) {
			// TODO: 老方式暂不实现
			fflush(mem_stream);
			fclose(mem_stream);
			free(mem_buf);
			return NULL;
		} else if(checkbinaryattach(buf, article_stream, &attach_file_size)) {
			attach_no++;
			attach_filename = buf + 18;
			fprintf(mem_stream, "#attach %s\n", attach_filename);
			memset(link, 0, 256);
			snprintf(link, 256, "http://%s:8080/%s/%s/%d/%s", MY_BBS_DOMAIN,
					bname, fname, -4+(int)ftell(article_stream), attach_filename);
			add_attach_link(&attach_link_list, link, attach_file_size);
			fseek(article_stream, attach_file_size, SEEK_CUR);
			continue;
		}

		// 常规字符处理
		if(mode == ARTICLE_PARSE_WITHOUT_ANSICOLOR
				&& strchr(buf, '\033') != NULL) {
			tmp_buf = strdup(buf);

			while(strchr(tmp_buf, '\033') != NULL)
				tmp_buf = string_replace(tmp_buf, "\033", "[ESC]");

			fprintf(mem_stream, "%s", tmp_buf);
			free(tmp_buf);
			tmp_buf = NULL;
		} else{
			fprintf(mem_stream, "%s", buf);
		}
	}
	fflush(mem_stream);
	fclose(article_stream);

	char *utf_content;
	if(mode == ARTICLE_PARSE_WITHOUT_ANSICOLOR) { // 不包含 '\033'，直接转码
		utf_content = (char *)malloc(3*mem_buf_len);
		memset(utf_content, 0, 3*mem_buf_len);
		g2u(mem_buf, mem_buf_len, utf_content, 3*mem_buf_len);
	} else { // 将 ansi 色彩转为 HTML 标记
		html_stream = open_memstream(&html_buf, &html_buf_len);
		fseek(mem_stream, 0, SEEK_SET);
		fprintf(html_stream, "<article>\n");

		aha_convert(mem_stream, html_stream);

		fprintf(html_stream, "</article>");

		fflush(html_stream);
		fclose(html_stream);

		utf_content = (char*)malloc(3*html_buf_len);
		memset(utf_content, 0, 3*html_buf_len);
		g2u(html_buf, html_buf_len, utf_content, 3*html_buf_len);
		free(html_buf);
	}

	// 释放资源
	fclose(mem_stream);
	free(mem_buf);

	return utf_content;
}

void aha_convert(FILE *in_stream, FILE *out_stream)
{
	char line_break=0;
	unsigned int c;
	int fc = -1; //Standard Foreground Color //IRC-Color+8
	int bc = -1; //Standard Background Color //IRC-Color+8
	int ul = 0; //Not underlined
	int bo = 0; //Not bold
	int bl = 0; //No Blinking
	int ofc,obc,oul,obo,obl; //old values
	int line=0;
	int momline=0;
	int newline=-1;
	int temp;

	int future=0;
	int future_char=0;

	while((c=fgetc(in_stream)) != EOF) {
		if(c=='\033') {
			//Saving old values
			ofc=fc;
			obc=bc;
			oul=ul;
			obo=bo;
			obl=bl;
			//Searching the end (a letter) and safe the insert:
			c='0';
			char buffer[1024];
			int counter=0;
			while ((c<'A') || ((c>'Z') && (c<'a')) || (c>'z')) {
				c=getNextChar(in_stream, &future, &future_char);
				buffer[counter]=c;
				if (c=='>') //end of htop
					break;
				counter++;
				if (counter>1022)
					break;
			}
			buffer[counter-1]=0;
			pelem elem;
			switch (c) {
			case 'm':
				//printf("\n%s\n",buffer); //DEBUG
				elem=parseInsert(buffer);
				pelem momelem=elem;
				while (momelem!=NULL) {
					//jump over zeros
					int mompos=0;
					while (mompos<momelem->digitcount && momelem->digit[mompos]==0)
						mompos++;
					if (mompos==momelem->digitcount) //only zeros => delete all
					{
						bo=0;ul=0;bl=0;fc=-1;bc=-1;
					}
					else
					{
						switch (momelem->digit[mompos])
						{
							case 1: bo=1; break;
							case 2: if (mompos+1<momelem->digitcount)
											switch (momelem->digit[mompos+1])
											{
												case 1: //Reset blink and bold
													bo=0;
													bl=0;
													break;
												case 4: //Reset underline
													ul=0;
													break;
													case 7: //Reset Inverted
													temp = bc;
													if (fc == -1 || fc == 9)
													{
															bc = 0;
													}
													else
														bc = fc;
													if (temp == -1 || temp == 9)
													{
															fc = 7;
													}
													else
														fc = temp;
													break;
											}
											break;
					case 3: if (mompos+1<momelem->digitcount)
										fc=momelem->digit[mompos+1];
									break;
					case 4: if (mompos+1==momelem->digitcount)
										ul=1;
									else
										bc=momelem->digit[mompos+1];
									break;
					case 5: bl=1; break;
					case 7: //TODO: Inverse
									temp = bc;
									if (fc == -1 || fc == 9)
									{
											bc = 0;
									}
									else
										bc = fc;
									if (temp == -1 || temp == 9)
									{
											fc = 7;
									}
									else
										fc = temp;
									break;
						}
					}
					momelem=momelem->next;
				}
				deleteParse(elem);
			break;
			case 'H': break;
			}

			//Checking the differeces
			if ((fc!=ofc) || (bc!=obc) || (ul!=oul) || (bo!=obo) || (bl!=obl)) //ANY Change
			{
				if ((ofc!=-1) || (obc!=-1) || (oul!=0) || (obo!=0) || (obl!=0))
					fprintf(out_stream, "</span>");
				if ((fc!=-1) || (bc!=-1) || (ul!=0) || (bo!=0) || (bl!=0))
				{
					fprintf(out_stream, "<span style=\"");
					switch (fc)
					{
						case	0: fprintf(out_stream, "color:black;"); break; //Black
						case	1: fprintf(out_stream, "color:red;"); break; //Red
						case	2: fprintf(out_stream, "color:green;"); break; //Green
						case	3: fprintf(out_stream, "color:olive;"); break; //Yellow
						case	4: fprintf(out_stream, "color:blue;"); break; //Blue
						case	5: fprintf(out_stream, "color:purple;"); break; //Purple
						case	6: fprintf(out_stream, "color:teal;"); break; //Cyan
						case	7: fprintf(out_stream, "color:gray;"); break; //White
						case	9: fprintf(out_stream, "color:black;"); break; //Reset
					}
					switch (bc)
					{
						//case -1: printf("background-color:white; "); break; //StandardColor
						case	0: fprintf(out_stream, "background-color:black;"); break; //Black
						case	1: fprintf(out_stream, "background-color:red;"); break; //Red
						case	2: fprintf(out_stream, "background-color:green;"); break; //Green
						case	3: fprintf(out_stream, "background-color:olive;");  break; //Yellow
						case	4: fprintf(out_stream, "background-color:blue;"); break; //Blue
						case	5: fprintf(out_stream, "background-color:purple;"); break; //Purple
						case	6: fprintf(out_stream, "background-color:teal;"); break; //Cyan
						case	7: fprintf(out_stream, "background-color:gray;"); break; //White
						case	9: fprintf(out_stream, "background-color:white;"); break; //Reset
					}
					if (ul)
						fprintf(out_stream, "text-decoration:underline;");
					if (bo)
						fprintf(out_stream, "font-weight:bold;");
					if (bl)
						fprintf(out_stream, "text-decoration:blink;");

					fprintf(out_stream, "\">");
				}
			}
		} else if(c!='\b'){
			line++;
			if (line_break) {
				fprintf(out_stream, "\n");
				line=0;
				line_break=0;
				momline++;
			}
			if (newline>=0) {
				while (newline>line) {
					fprintf(out_stream, " ");
					line++;
				}
				newline=-1;
			}
			switch (c) {
			case '&':	fprintf(out_stream, "&amp;"); break;
			case '\"': 	fprintf(out_stream, "&quot;"); break;
			case '<':	fprintf(out_stream, "&lt;"); break;
			case '>':	fprintf(out_stream, "&gt;"); break;
			case '\n':
			case 13: 	momline++;line=0;
						fprintf(out_stream, "<br />\n"); break;
			default:	fprintf(out_stream, "%c",c);
			}
		}
	}

	if ((fc!=-1) || (bc!=-1) || (ul!=0) || (bo!=0) || (bl!=0))
		fprintf(out_stream, "</span>\n");
}
