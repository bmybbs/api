#include "bbs.h"
#include "ythtlib.h"
#include "ythtbbs.h"

//static struct allbrc allbrc;
//static char allbrcuser[STRLEN];
//static struct onebrc *pbrc, brc;

int readuserallbrc(char *userid, struct allbrc *allbrc, char *allbrcuser, const char *fromhost, int must)
{
	char buf[STRLEN];
	if (!userid)
		return 0;
	if (strcasecmp(userid, "guest")==0) {
		snprintf(buf, sizeof (buf), "guest.%s", fromhost);
		if (!must && !strncmp(allbrcuser, buf, STRLEN))
			return 0;
		strsncpy(allbrcuser, buf, STRLEN);
		brc_init(allbrc, allbrcuser, NULL);

	} else {
		if (!must && !strncmp(allbrcuser, userid, STRLEN))
			return 0;
		sethomefile(buf, userid, "brc");
		strsncpy(allbrcuser, userid, sizeof (allbrcuser));
		brc_init(allbrc, userid, buf);
	}
	return 0;
}

void brc_update(char *userid, struct allbrc *allbrc, char *allbrcuser, struct onebrc *pbrc, const char *fromhost)
{
	if (!pbrc->changed)
		return;
	readuserallbrc(userid, allbrc, allbrcuser, fromhost, 0);
	brc_putboard(allbrc, pbrc);
	if (strcasecmp(userid, "guest")==0) {
		char str[STRLEN];
		sprintf(str, "guest.%s", fromhost);
		brc_fini(allbrc, str);
	} else
		brc_fini(allbrc, userid);
}

int brc_initial(char *userid, char *boardname,struct allbrc *allbrc, char *allbrcuser, const char *fromhost, struct user_info *u_info, struct onebrc **pbrc, struct onebrc *brc)
{
	if (u_info)
		*pbrc = &u_info->brc;
	else {
		*pbrc = brc;
		memset(brc, 0, sizeof (struct onebrc));
	}
	if (boardname && !strncmp(boardname, (*pbrc)->board, sizeof ((*pbrc)->board)))
		return 0;
	readuserallbrc(userid, allbrc, allbrcuser, fromhost, 1);
	if (boardname)
		brc_getboard(allbrc, *pbrc, boardname);
	return 0;
}

void brc_add_read(struct fileheader *fh, struct onebrc *pbrc)
{
	SETREAD(fh, pbrc);
}

void brc_add_readt(int t, struct onebrc *pbrc)
{
	brc_addlistt(pbrc, t);
}

int brc_un_read(struct fileheader *fh, struct onebrc *pbrc)
{
	return UNREAD(fh, pbrc);
}

void
brc_clear(struct onebrc *pbrc)
{
	brc_clearto(pbrc, time(NULL));
}

int
brc_un_read_time(int ftime, struct onebrc *pbrc)
{
	return brc_unreadt(pbrc, ftime);
}
