#ifndef HBS_UTILS_H
#define HBS_UTILS_H

#include <stddef.h>
#include <stdbool.h>

/* Dynamic string buffer */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} hbs_strbuf_t;

void hbs_strbuf_init(hbs_strbuf_t *buf);
void hbs_strbuf_append(hbs_strbuf_t *buf, const char *str);
void hbs_strbuf_appendn(hbs_strbuf_t *buf, const char *str, size_t n);
void hbs_strbuf_append_char(hbs_strbuf_t *buf, char c);
char *hbs_strbuf_detach(hbs_strbuf_t *buf);
void hbs_strbuf_free(hbs_strbuf_t *buf);

/* HTML escaping */
char *hbs_html_escape(const char *input);

/* Whitespace trimming */
void hbs_trim_right(char *str);
void hbs_trim_left_buf(hbs_strbuf_t *buf);

#endif /* HBS_UTILS_H */
