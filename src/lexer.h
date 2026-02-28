#ifndef HBS_LEXER_H
#define HBS_LEXER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    HBS_TOK_TEXT,           /* Literal text outside {{ }} */
    HBS_TOK_OPEN,           /* {{ */
    HBS_TOK_OPEN_UNESC,     /* {{{ */
    HBS_TOK_OPEN_UNESC_AMP, /* {{& */
    HBS_TOK_CLOSE,          /* }} */
    HBS_TOK_CLOSE_UNESC,    /* }}} */
    HBS_TOK_OPEN_BLOCK,     /* {{# */
    HBS_TOK_OPEN_END_BLOCK, /* {{/ */
    HBS_TOK_OPEN_PARTIAL,   /* {{> */
    HBS_TOK_OPEN_PARTIAL_BLOCK, /* {{#> */
    HBS_TOK_OPEN_COMMENT,   /* {{! or {{!-- */
    HBS_TOK_OPEN_RAW_BLOCK, /* {{{{ */
    HBS_TOK_CLOSE_RAW_BLOCK,/* }}}} */
    HBS_TOK_OPEN_INVERSE,   /* {{^ */
    HBS_TOK_OPEN_DECORATOR, /* {{#* */
    HBS_TOK_ID,             /* Identifier */
    HBS_TOK_DOT,            /* . separator */
    HBS_TOK_DOT_DOT,        /* .. parent reference */
    HBS_TOK_SLASH,          /* / path separator */
    HBS_TOK_EQUALS,         /* = in hash args */
    HBS_TOK_STRING,         /* "string" or 'string' */
    HBS_TOK_NUMBER,         /* 123 or 1.5 */
    HBS_TOK_BOOLEAN,        /* true or false */
    HBS_TOK_NULL,           /* null */
    HBS_TOK_UNDEFINED,      /* undefined */
    HBS_TOK_OPEN_PAREN,     /* ( subexpression */
    HBS_TOK_CLOSE_PAREN,    /* ) subexpression */
    HBS_TOK_PIPE,           /* | block params */
    HBS_TOK_STRIP,          /* ~ whitespace control */
    HBS_TOK_SEG_LITERAL,    /* [literal-segment] */
    HBS_TOK_AS,             /* as keyword */
    HBS_TOK_COMMENT_BODY,   /* Comment content */
    HBS_TOK_EOF
} hbs_token_type_t;

typedef struct {
    hbs_token_type_t type;
    char *value;
    int line;
    int col;
} hbs_token_t;

typedef struct {
    const char *source;
    size_t pos;
    size_t len;
    int line;
    int col;
    bool in_tag;                /* Currently between {{ and }} */
    hbs_token_t *tokens;
    int token_count;
    int token_cap;
} hbs_lexer_t;

void hbs_lexer_init(hbs_lexer_t *lexer, const char *source);
int hbs_lexer_tokenize(hbs_lexer_t *lexer);
void hbs_lexer_free(hbs_lexer_t *lexer);

#endif /* HBS_LEXER_H */
