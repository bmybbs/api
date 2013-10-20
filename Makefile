CC		= gcc
BSRCPTH	= /home/bmybbs
FLAGS 	= -O -Wall -g -D_GNU_SOURCE -I$(BSRCPTH)/include -I$(BSRCPTH)/ythtlib -I$(BSRCPTH)/libythtbbs
BBSLIBS	= -L/home/bbs/bin -lythtbbs -lytht -lmysqlclient -lxml2
ONILIBS = -lonion_handlers -lonion_static -lpam -lgnutls -lgcrypt -pthread -lrt

PROGNAME = bmyapi
CFILES	:= main.c api_error.c api_template.c api_user.c \
		   apilib.c
		   
COBJS	:= $(CFILES:.c=.o)
.c.o	;  $(CC) -c $*.c $(FLAGS)

all: $(PROGNAME)

$(PROGNAME): $(COBJS)
	$(CC) -o $@ $^ $(BBSLIBS) $(ONILIBS)