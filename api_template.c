#include "apilib.h"

static char *template_string_replace(char *ori, const char *old, const char *new);

api_template_t api_template_create(const char *filename)
{
	char *p, *s;
	int fd;
	struct stat statbuf;

	fd = open(filename, O_RDONLY);
	if(fd == -1) {
		return NULL; // cannot open
	}

	if(fstat(fd, &statbuf) == -1) {
		close(fd);
		return NULL; // fstat error
	}

	if(!S_ISREG(statbuf.st_mode)) {
		close(fd);
		return NULL; // not a file
	}

	p = mmap(0, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);

	if(p == MAP_FAILED) {
		return NULL;
	}

	s = strdup(p);

	munmap(p, statbuf.st_size);
	return s;
}

void api_template_set(api_template_t *tpl, const char *key, char *fmt, ...)
{
	if(!tpl)
		return;

	va_list v;
	char *new_string, *old_string;

	va_start(v, fmt);
	vasprintf(&new_string, fmt, v);
	va_end(v);

	// old_string = "<% key %>"
	old_string = malloc(strlen(key) + 7);
	sprintf(old_string, "<%% %s %%>", key);

	*tpl = template_string_replace(*tpl, old_string, new_string);

	free(new_string);
	free(old_string);
}

static char *template_string_replace(char *ori, const char *old, const char *new)
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
	*(tmp_string + tmp_string_length) = 0;

	free(ori);
	ori = tmp_string;

	return ori;
}

void api_template_free(api_template_t tpl)
{
	free(tpl);
}
