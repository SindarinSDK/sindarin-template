#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#define INITIAL_TOKEN_CAP 64

static void add_token(hbs_lexer_t *lex, hbs_token_type_t type, const char *value, int line, int col) {
    if (lex->token_count >= lex->token_cap) {
        lex->token_cap *= 2;
        lex->tokens = realloc(lex->tokens, sizeof(hbs_token_t) * lex->token_cap);
    }
    hbs_token_t *tok = &lex->tokens[lex->token_count++];
    tok->type = type;
    tok->value = value ? strdup(value) : NULL;
    tok->line = line;
    tok->col = col;
}

static char peek(hbs_lexer_t *lex, int offset) {
    size_t idx = lex->pos + offset;
    if (idx >= lex->len) return '\0';
    return lex->source[idx];
}

static char advance(hbs_lexer_t *lex) {
    char c = lex->source[lex->pos++];
    if (c == '\n') {
        lex->line++;
        lex->col = 1;
    } else {
        lex->col++;
    }
    return c;
}

static bool match_str(hbs_lexer_t *lex, const char *str) {
    size_t slen = strlen(str);
    if (lex->pos + slen > lex->len) return false;
    return strncmp(lex->source + lex->pos, str, slen) == 0;
}

static void skip_whitespace(hbs_lexer_t *lex) {
    while (lex->pos < lex->len && isspace((unsigned char)lex->source[lex->pos])) {
        advance(lex);
    }
}

static void lex_text(hbs_lexer_t *lex) {
    int start_line = lex->line;
    int start_col = lex->col;

    /* Build text manually to handle \{{ -> {{ stripping */
    size_t buf_cap = 256;
    size_t buf_len = 0;
    char *buf = malloc(buf_cap);

    while (lex->pos < lex->len) {
        /* Check for escaped mustache: \{{ -> output {{ literally */
        if (lex->source[lex->pos] == '\\' && peek(lex, 1) == '{' && peek(lex, 2) == '{') {
            advance(lex); /* skip the backslash */
            /* Append {{ to buffer */
            if (buf_len + 2 >= buf_cap) { buf_cap *= 2; buf = realloc(buf, buf_cap); }
            buf[buf_len++] = '{';
            buf[buf_len++] = '{';
            advance(lex); /* { */
            advance(lex); /* { */
            continue;
        }
        /* Check for opening mustache */
        if (match_str(lex, "{{")) {
            break;
        }
        if (buf_len + 1 >= buf_cap) { buf_cap *= 2; buf = realloc(buf, buf_cap); }
        buf[buf_len++] = lex->source[lex->pos];
        advance(lex);
    }

    if (buf_len > 0) {
        buf[buf_len] = '\0';
        add_token(lex, HBS_TOK_TEXT, buf, start_line, start_col);
    }
    free(buf);
}

static void lex_comment(hbs_lexer_t *lex) {
    int start_line = lex->line;
    int start_col = lex->col;

    /* Determine if long comment ({{!-- ... --}}) */
    bool long_comment = match_str(lex, "--");
    if (long_comment) {
        lex->pos += 2;
        lex->col += 2;
    }

    size_t start = lex->pos;

    if (long_comment) {
        while (lex->pos < lex->len && !match_str(lex, "--}}")) {
            advance(lex);
        }
    } else {
        while (lex->pos < lex->len && !match_str(lex, "}}")) {
            advance(lex);
        }
    }

    size_t comment_len = lex->pos - start;
    char *comment = malloc(comment_len + 1);
    memcpy(comment, lex->source + start, comment_len);
    comment[comment_len] = '\0';
    add_token(lex, HBS_TOK_COMMENT_BODY, comment, start_line, start_col);
    free(comment);

    if (long_comment && match_str(lex, "--}}")) {
        lex->pos += 4;
        lex->col += 4;
    } else if (match_str(lex, "}}")) {
        lex->pos += 2;
        lex->col += 2;
    }

    lex->in_tag = false;
}

static void lex_string(hbs_lexer_t *lex) {
    char quote = advance(lex); /* consume opening quote */
    int start_line = lex->line;
    int start_col = lex->col;
    size_t start = lex->pos;

    while (lex->pos < lex->len && lex->source[lex->pos] != quote) {
        if (lex->source[lex->pos] == '\\') advance(lex); /* skip escape */
        advance(lex);
    }

    size_t str_len = lex->pos - start;
    char *str = malloc(str_len + 1);
    memcpy(str, lex->source + start, str_len);
    str[str_len] = '\0';
    add_token(lex, HBS_TOK_STRING, str, start_line, start_col);
    free(str);

    if (lex->pos < lex->len) advance(lex); /* consume closing quote */
}

static void lex_number(hbs_lexer_t *lex) {
    int start_line = lex->line;
    int start_col = lex->col;
    size_t start = lex->pos;

    if (lex->source[lex->pos] == '-') advance(lex);
    while (lex->pos < lex->len && isdigit((unsigned char)lex->source[lex->pos])) {
        advance(lex);
    }
    if (lex->pos < lex->len && lex->source[lex->pos] == '.') {
        advance(lex);
        while (lex->pos < lex->len && isdigit((unsigned char)lex->source[lex->pos])) {
            advance(lex);
        }
    }

    size_t num_len = lex->pos - start;
    char *num = malloc(num_len + 1);
    memcpy(num, lex->source + start, num_len);
    num[num_len] = '\0';
    add_token(lex, HBS_TOK_NUMBER, num, start_line, start_col);
    free(num);
}

static void lex_identifier(hbs_lexer_t *lex) {
    int start_line = lex->line;
    int start_col = lex->col;
    size_t start = lex->pos;
    bool starts_with_at = (lex->source[lex->pos] == '@');

    /* @ is only valid at the start of an identifier */
    if (starts_with_at) advance(lex);

    while (lex->pos < lex->len) {
        char c = lex->source[lex->pos];
        if (isalnum((unsigned char)c) || c == '_' || c == '-') {
            advance(lex);
        } else {
            break;
        }
    }

    size_t id_len = lex->pos - start;
    char *id = malloc(id_len + 1);
    memcpy(id, lex->source + start, id_len);
    id[id_len] = '\0';

    /* Check for keywords (only non-@ identifiers) */
    if (!starts_with_at) {
        if (strcmp(id, "true") == 0 || strcmp(id, "false") == 0) {
            add_token(lex, HBS_TOK_BOOLEAN, id, start_line, start_col);
            free(id);
            return;
        } else if (strcmp(id, "null") == 0) {
            add_token(lex, HBS_TOK_NULL, id, start_line, start_col);
            free(id);
            return;
        } else if (strcmp(id, "undefined") == 0) {
            add_token(lex, HBS_TOK_UNDEFINED, id, start_line, start_col);
            free(id);
            return;
        } else if (strcmp(id, "as") == 0) {
            add_token(lex, HBS_TOK_AS, id, start_line, start_col);
            free(id);
            return;
        }
    }

    add_token(lex, HBS_TOK_ID, id, start_line, start_col);
    free(id);
}

static void lex_segment_literal(hbs_lexer_t *lex) {
    advance(lex); /* consume [ */
    int start_line = lex->line;
    int start_col = lex->col;
    size_t start = lex->pos;

    while (lex->pos < lex->len && lex->source[lex->pos] != ']') {
        advance(lex);
    }

    size_t seg_len = lex->pos - start;
    char *seg = malloc(seg_len + 1);
    memcpy(seg, lex->source + start, seg_len);
    seg[seg_len] = '\0';
    add_token(lex, HBS_TOK_SEG_LITERAL, seg, start_line, start_col);
    free(seg);

    if (lex->pos < lex->len) advance(lex); /* consume ] */
}

static void lex_tag_contents(hbs_lexer_t *lex) {
    while (lex->pos < lex->len && lex->in_tag) {
        skip_whitespace(lex);
        if (lex->pos >= lex->len) break;

        char c = lex->source[lex->pos];

        /* Check for close tokens (longest match first) */
        if (match_str(lex, "}}}}")) {
            add_token(lex, HBS_TOK_CLOSE_RAW_BLOCK, NULL, lex->line, lex->col);
            lex->pos += 4; lex->col += 4;
            lex->in_tag = false;
            return;
        }
        if (match_str(lex, "}}}")) {
            add_token(lex, HBS_TOK_CLOSE_UNESC, NULL, lex->line, lex->col);
            lex->pos += 3; lex->col += 3;
            lex->in_tag = false;
            return;
        }
        if (match_str(lex, "}}")) {
            add_token(lex, HBS_TOK_CLOSE, NULL, lex->line, lex->col);
            lex->pos += 2; lex->col += 2;
            lex->in_tag = false;
            return;
        }

        if (c == '~') {
            add_token(lex, HBS_TOK_STRIP, NULL, lex->line, lex->col);
            advance(lex);
            continue;
        }

        if (c == '.' && peek(lex, 1) == '.') {
            add_token(lex, HBS_TOK_DOT_DOT, "..", lex->line, lex->col);
            advance(lex); advance(lex);
            continue;
        }

        if (c == '.') {
            add_token(lex, HBS_TOK_DOT, ".", lex->line, lex->col);
            advance(lex);
            continue;
        }

        if (c == '/') {
            add_token(lex, HBS_TOK_SLASH, "/", lex->line, lex->col);
            advance(lex);
            continue;
        }

        if (c == '=') {
            add_token(lex, HBS_TOK_EQUALS, "=", lex->line, lex->col);
            advance(lex);
            continue;
        }

        if (c == '(') {
            add_token(lex, HBS_TOK_OPEN_PAREN, NULL, lex->line, lex->col);
            advance(lex);
            continue;
        }
        if (c == ')') {
            add_token(lex, HBS_TOK_CLOSE_PAREN, NULL, lex->line, lex->col);
            advance(lex);
            continue;
        }

        if (c == '|') {
            add_token(lex, HBS_TOK_PIPE, NULL, lex->line, lex->col);
            advance(lex);
            continue;
        }

        if (c == '[') {
            lex_segment_literal(lex);
            continue;
        }

        if (c == '"' || c == '\'') {
            lex_string(lex);
            continue;
        }

        if (isdigit((unsigned char)c) || (c == '-' && isdigit((unsigned char)peek(lex, 1)))) {
            lex_number(lex);
            continue;
        }

        if (isalpha((unsigned char)c) || c == '_' || c == '@') {
            lex_identifier(lex);
            continue;
        }

        /* Unknown character - skip */
        advance(lex);
    }
}

/* Lex raw block content: everything between {{{{helper}}}} and {{{{/helper}}}} */
static void lex_raw_block_content(hbs_lexer_t *lex, const char *helper_name) {
    size_t start = lex->pos;
    int start_line = lex->line;
    int start_col = lex->col;

    /* Build the close tag to search for: {{{{/helper_name}}}} */
    size_t close_len = 5 + strlen(helper_name) + 4; /* {{{{/ + name + }}}} */
    char *close_tag = malloc(close_len + 1);
    snprintf(close_tag, close_len + 1, "{{{{/%s}}}}", helper_name);

    while (lex->pos < lex->len) {
        if (match_str(lex, close_tag)) {
            break;
        }
        advance(lex);
    }

    /* Emit raw content as text */
    size_t content_len = lex->pos - start;
    if (content_len > 0) {
        char *content = malloc(content_len + 1);
        memcpy(content, lex->source + start, content_len);
        content[content_len] = '\0';
        add_token(lex, HBS_TOK_TEXT, content, start_line, start_col);
        free(content);
    }

    /* Consume the close tag */
    if (match_str(lex, close_tag)) {
        add_token(lex, HBS_TOK_OPEN_END_BLOCK, NULL, lex->line, lex->col);
        lex->pos += 5; lex->col += 5; /* {{{{/ */
        add_token(lex, HBS_TOK_ID, helper_name, lex->line, lex->col);
        lex->pos += strlen(helper_name); lex->col += strlen(helper_name);
        add_token(lex, HBS_TOK_CLOSE_RAW_BLOCK, NULL, lex->line, lex->col);
        lex->pos += 4; lex->col += 4; /* }}}} */
    }

    free(close_tag);
}

void hbs_lexer_init(hbs_lexer_t *lex, const char *source) {
    memset(lex, 0, sizeof(hbs_lexer_t));
    lex->source = source;
    lex->len = strlen(source);
    lex->line = 1;
    lex->col = 1;
    lex->token_cap = INITIAL_TOKEN_CAP;
    lex->tokens = malloc(sizeof(hbs_token_t) * lex->token_cap);
}

int hbs_lexer_tokenize(hbs_lexer_t *lex) {
    while (lex->pos < lex->len) {
        if (!lex->in_tag) {
            /* Raw block open: {{{{ */
            if (match_str(lex, "{{{{") && !match_str(lex, "{{{{/")) {
                lex_text(lex);
                add_token(lex, HBS_TOK_OPEN_RAW_BLOCK, NULL, lex->line, lex->col);
                lex->pos += 4; lex->col += 4;
                lex->in_tag = true;
                lex_tag_contents(lex);
                /* After the open tag closes, find the helper name and lex raw content */
                /* The helper name is the first ID token after OPEN_RAW_BLOCK */
                char *helper_name = NULL;
                for (int i = lex->token_count - 1; i >= 0; i--) {
                    if (lex->tokens[i].type == HBS_TOK_OPEN_RAW_BLOCK) {
                        /* Next token should be the ID */
                        if (i + 1 < lex->token_count && lex->tokens[i + 1].type == HBS_TOK_ID) {
                            helper_name = lex->tokens[i + 1].value;
                        }
                        break;
                    }
                }
                if (helper_name) {
                    lex_raw_block_content(lex, helper_name);
                }
                continue;
            }

            /* Triple-stash open: {{{ */
            if (match_str(lex, "{{{")) {
                lex_text(lex);
                add_token(lex, HBS_TOK_OPEN_UNESC, NULL, lex->line, lex->col);
                lex->pos += 3; lex->col += 3;
                lex->in_tag = true;
                lex_tag_contents(lex);
                continue;
            }

            /* Double-stash open: {{ */
            if (match_str(lex, "{{")) {
                lex_text(lex);

                int line = lex->line, col = lex->col;
                lex->pos += 2; lex->col += 2;

                char next = lex->pos < lex->len ? lex->source[lex->pos] : '\0';
                char next2 = (lex->pos + 1) < lex->len ? lex->source[lex->pos + 1] : '\0';

                if (next == '#' && next2 == '>') {
                    /* {{#> partial block */
                    add_token(lex, HBS_TOK_OPEN_PARTIAL_BLOCK, NULL, line, col);
                    advance(lex); advance(lex);
                } else if (next == '#' && next2 == '*') {
                    /* {{#* decorator (inline partial) */
                    add_token(lex, HBS_TOK_OPEN_DECORATOR, NULL, line, col);
                    advance(lex); advance(lex);
                } else if (next == '#') {
                    add_token(lex, HBS_TOK_OPEN_BLOCK, NULL, line, col);
                    advance(lex);
                } else if (next == '/') {
                    add_token(lex, HBS_TOK_OPEN_END_BLOCK, NULL, line, col);
                    advance(lex);
                } else if (next == '>') {
                    add_token(lex, HBS_TOK_OPEN_PARTIAL, NULL, line, col);
                    advance(lex);
                } else if (next == '!') {
                    add_token(lex, HBS_TOK_OPEN_COMMENT, NULL, line, col);
                    advance(lex);
                    lex->in_tag = true;
                    lex_comment(lex);
                    continue;
                } else if (next == '^') {
                    add_token(lex, HBS_TOK_OPEN_INVERSE, NULL, line, col);
                    advance(lex);
                } else if (next == '&') {
                    add_token(lex, HBS_TOK_OPEN_UNESC_AMP, NULL, line, col);
                    advance(lex);
                } else {
                    add_token(lex, HBS_TOK_OPEN, NULL, line, col);
                }

                lex->in_tag = true;
                lex_tag_contents(lex);
                continue;
            }

            /* Regular text */
            lex_text(lex);
        }
    }

    add_token(lex, HBS_TOK_EOF, NULL, lex->line, lex->col);
    return 0;
}

void hbs_lexer_free(hbs_lexer_t *lex) {
    for (int i = 0; i < lex->token_count; i++) {
        free(lex->tokens[i].value);
    }
    free(lex->tokens);
    lex->tokens = NULL;
    lex->token_count = 0;
}
