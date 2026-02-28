#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_CAP 256

void hbs_strbuf_init(hbs_strbuf_t *buf) {
    buf->data = malloc(INITIAL_CAP);
    buf->data[0] = '\0';
    buf->len = 0;
    buf->cap = INITIAL_CAP;
}

static void hbs_strbuf_grow(hbs_strbuf_t *buf, size_t needed) {
    if (buf->len + needed + 1 > buf->cap) {
        while (buf->cap < buf->len + needed + 1) {
            buf->cap *= 2;
        }
        buf->data = realloc(buf->data, buf->cap);
    }
}

void hbs_strbuf_append(hbs_strbuf_t *buf, const char *str) {
    size_t slen = strlen(str);
    hbs_strbuf_grow(buf, slen);
    memcpy(buf->data + buf->len, str, slen);
    buf->len += slen;
    buf->data[buf->len] = '\0';
}

void hbs_strbuf_appendn(hbs_strbuf_t *buf, const char *str, size_t n) {
    hbs_strbuf_grow(buf, n);
    memcpy(buf->data + buf->len, str, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
}

void hbs_strbuf_append_char(hbs_strbuf_t *buf, char c) {
    hbs_strbuf_grow(buf, 1);
    buf->data[buf->len++] = c;
    buf->data[buf->len] = '\0';
}

char *hbs_strbuf_detach(hbs_strbuf_t *buf) {
    char *result = buf->data;
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    return result;
}

void hbs_strbuf_free(hbs_strbuf_t *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

char *hbs_html_escape(const char *input) {
    if (!input) return strdup("");

    hbs_strbuf_t buf;
    hbs_strbuf_init(&buf);

    for (const char *p = input; *p; p++) {
        switch (*p) {
            case '&':  hbs_strbuf_append(&buf, "&amp;");  break;
            case '<':  hbs_strbuf_append(&buf, "&lt;");   break;
            case '>':  hbs_strbuf_append(&buf, "&gt;");   break;
            case '"':  hbs_strbuf_append(&buf, "&quot;");  break;
            case '\'': hbs_strbuf_append(&buf, "&#x27;"); break;
            case '`':  hbs_strbuf_append(&buf, "&#x60;"); break;
            case '=':  hbs_strbuf_append(&buf, "&#x3D;"); break;
            default:   hbs_strbuf_append_char(&buf, *p);  break;
        }
    }

    return hbs_strbuf_detach(&buf);
}

void hbs_trim_right(char *str) {
    if (!str) return;
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

void hbs_trim_left_buf(hbs_strbuf_t *buf) {
    if (!buf || !buf->data || buf->len == 0) return;
    size_t i = 0;
    while (i < buf->len && isspace((unsigned char)buf->data[i])) {
        i++;
    }
    if (i > 0) {
        memmove(buf->data, buf->data + i, buf->len - i);
        buf->len -= i;
        buf->data[buf->len] = '\0';
    }
}
