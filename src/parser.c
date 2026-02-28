#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static hbs_token_t *current(hbs_parser_t *p) {
    if (p->pos >= p->token_count) return NULL;
    return &p->tokens[p->pos];
}

static hbs_token_t *peek_token(hbs_parser_t *p, int offset) {
    int idx = p->pos + offset;
    if (idx < 0 || idx >= p->token_count) return NULL;
    return &p->tokens[idx];
}

static hbs_token_t *consume(hbs_parser_t *p) {
    if (p->pos >= p->token_count) return NULL;
    return &p->tokens[p->pos++];
}

static bool check(hbs_parser_t *p, hbs_token_type_t type) {
    hbs_token_t *tok = current(p);
    return tok && tok->type == type;
}

static bool expect(hbs_parser_t *p, hbs_token_type_t type) {
    if (check(p, type)) {
        consume(p);
        return true;
    }
    return false;
}

static void parser_error(hbs_parser_t *p, const char *msg) {
    if (!p->error) {
        hbs_token_t *tok = current(p);
        char buf[256];
        if (tok) {
            snprintf(buf, sizeof(buf), "Parse error at line %d, col %d: %s", tok->line, tok->col, msg);
        } else {
            snprintf(buf, sizeof(buf), "Parse error at end of input: %s", msg);
        }
        p->error = strdup(buf);
    }
}

/* Forward declarations */
static hbs_ast_node_t *parse_mustache(hbs_parser_t *p, bool escaped, hbs_token_type_t close_tok);
static hbs_ast_node_t *parse_block(hbs_parser_t *p);
static hbs_ast_node_t *parse_comment(hbs_parser_t *p);
static hbs_ast_node_t *parse_partial(hbs_parser_t *p);
static hbs_ast_node_t *parse_partial_block(hbs_parser_t *p);
static hbs_ast_node_t *parse_inline_partial(hbs_parser_t *p);
static hbs_ast_node_t *parse_raw_block(hbs_parser_t *p);
static hbs_ast_node_t *parse_program(hbs_parser_t *p, bool is_root);
static hbs_ast_node_t *parse_expression(hbs_parser_t *p);
static hbs_ast_node_t *parse_subexpr(hbs_parser_t *p);

/* ---- Path parsing ---- */

static hbs_path_t *parse_path(hbs_parser_t *p) {
    hbs_path_t *path = hbs_path_create();

    /* Handle ./ prefix (explicit context, skip helper lookup) */
    if (check(p, HBS_TOK_DOT) && peek_token(p, 1) &&
        peek_token(p, 1)->type == HBS_TOK_SLASH) {
        path->is_context_explicit = true;
        consume(p); /* . */
        consume(p); /* / */
    }

    /* Handle @../var — bare @ followed by depth prefix */
    if (check(p, HBS_TOK_ID) && current(p)->value &&
        strcmp(current(p)->value, "@") == 0) {
        hbs_token_t *next = peek_token(p, 1);
        if (next && next->type == HBS_TOK_DOT_DOT) {
            consume(p); /* consume bare @ */
            while (check(p, HBS_TOK_DOT_DOT)) {
                consume(p);
                path->depth++;
                if (check(p, HBS_TOK_SLASH)) {
                    consume(p);
                }
            }
            /* Read the identifier and prepend @ */
            if (check(p, HBS_TOK_ID)) {
                hbs_token_t *tok = consume(p);
                char *at_name = malloc(strlen(tok->value) + 2);
                sprintf(at_name, "@%s", tok->value);
                hbs_path_add_part(path, at_name);
                free(at_name);
            }
            return path;
        }
    }

    /* Handle ../ parent references */
    while (check(p, HBS_TOK_DOT_DOT)) {
        consume(p);
        path->depth++;
        if (check(p, HBS_TOK_SLASH)) {
            consume(p);
        }
    }

    /* Handle standalone "." (this reference) */
    if (check(p, HBS_TOK_DOT)) {
        hbs_token_t *dot = consume(p);
        (void)dot;
        if (!check(p, HBS_TOK_ID) && !check(p, HBS_TOK_SEG_LITERAL)) {
            path->is_this = true;
            return path;
        }
        /* ".field" means this.field */
        path->is_this = true;
    }

    /* Parse identifier chain: a.b.c */
    if (check(p, HBS_TOK_ID) || check(p, HBS_TOK_SEG_LITERAL)) {
        hbs_token_t *tok = consume(p);

        /* "this" keyword is equivalent to "." */
        if (tok->type == HBS_TOK_ID && strcmp(tok->value, "this") == 0) {
            path->is_this = true;
            if (!check(p, HBS_TOK_DOT)) {
                return path;
            }
            consume(p); /* consume the dot after "this" */
            if (check(p, HBS_TOK_ID) || check(p, HBS_TOK_SEG_LITERAL)) {
                tok = consume(p);
                hbs_path_add_part(path, tok->value);
            } else {
                return path;
            }
        } else {
            hbs_path_add_part(path, tok->value);
        }

        while (check(p, HBS_TOK_DOT)) {
            /* Only consume dot if followed by ID or SEG_LITERAL (dotted path) */
            hbs_token_t *after_dot = peek_token(p, 1);
            if (!after_dot || (after_dot->type != HBS_TOK_ID &&
                               after_dot->type != HBS_TOK_SEG_LITERAL)) {
                break; /* Dot is not part of this path */
            }
            consume(p); /* consume . */
            tok = consume(p);
            hbs_path_add_part(path, tok->value);
        }
    }

    return path;
}

/* ---- Expression parsing (parameters, subexpressions, literals) ---- */

/* Check if the next tokens form a hash argument (ID = ...) */
static bool is_hash_arg(hbs_parser_t *p) {
    if (!check(p, HBS_TOK_ID)) return false;
    /* Make sure this isn't an @-prefixed data var */
    hbs_token_t *id = current(p);
    if (id->value && id->value[0] == '@') return false;
    hbs_token_t *next = peek_token(p, 1);
    return next && next->type == HBS_TOK_EQUALS;
}

/* Check if a token starts an expression (path, literal, subexpr) */
static bool is_expression_start(hbs_parser_t *p) {
    hbs_token_t *tok = current(p);
    if (!tok) return false;
    switch (tok->type) {
        case HBS_TOK_ID:
        case HBS_TOK_DOT:
        case HBS_TOK_DOT_DOT:
        case HBS_TOK_SEG_LITERAL:
        case HBS_TOK_STRING:
        case HBS_TOK_NUMBER:
        case HBS_TOK_BOOLEAN:
        case HBS_TOK_NULL:
        case HBS_TOK_UNDEFINED:
        case HBS_TOK_OPEN_PAREN:
            return true;
        default:
            return false;
    }
}

/* Parse a single expression: path, literal, or subexpression */
static hbs_ast_node_t *parse_expression(hbs_parser_t *p) {
    hbs_token_t *tok = current(p);
    if (!tok) return NULL;

    /* Subexpression: (helper args...) */
    if (tok->type == HBS_TOK_OPEN_PAREN) {
        return parse_subexpr(p);
    }

    /* Literals */
    if (tok->type == HBS_TOK_STRING) {
        consume(p);
        return hbs_ast_literal(HBS_LIT_STRING, tok->value);
    }
    if (tok->type == HBS_TOK_NUMBER) {
        consume(p);
        return hbs_ast_literal(HBS_LIT_NUMBER, tok->value);
    }
    if (tok->type == HBS_TOK_BOOLEAN) {
        consume(p);
        return hbs_ast_literal(HBS_LIT_BOOLEAN, tok->value);
    }
    if (tok->type == HBS_TOK_NULL) {
        consume(p);
        return hbs_ast_literal(HBS_LIT_NULL, tok->value);
    }
    if (tok->type == HBS_TOK_UNDEFINED) {
        consume(p);
        return hbs_ast_literal(HBS_LIT_UNDEFINED, tok->value);
    }

    /* Path expression */
    if (tok->type == HBS_TOK_ID || tok->type == HBS_TOK_DOT ||
        tok->type == HBS_TOK_DOT_DOT || tok->type == HBS_TOK_SEG_LITERAL) {
        hbs_path_t *path = parse_path(p);
        return hbs_ast_mustache(path, true);
    }

    return NULL;
}

/* Parse a subexpression: (helper arg1 arg2 key=val) */
static hbs_ast_node_t *parse_subexpr(hbs_parser_t *p) {
    expect(p, HBS_TOK_OPEN_PAREN);

    hbs_path_t *path = parse_path(p);
    hbs_ast_node_t *node = hbs_ast_subexpr(path);

    /* Parse positional params */
    while (is_expression_start(p) && !is_hash_arg(p) && !check(p, HBS_TOK_CLOSE_PAREN)) {
        hbs_ast_node_t *param = parse_expression(p);
        if (param) {
            node->subexpr.params = realloc(node->subexpr.params,
                sizeof(hbs_ast_node_t *) * (node->subexpr.param_count + 1));
            node->subexpr.params[node->subexpr.param_count++] = param;
        }
    }

    /* Parse hash arguments */
    while (is_hash_arg(p)) {
        hbs_token_t *key_tok = consume(p);
        consume(p); /* = */
        hbs_ast_node_t *val = parse_expression(p);
        if (val) {
            int idx = node->subexpr.hash_count;
            node->subexpr.hash_pairs = realloc(node->subexpr.hash_pairs,
                sizeof(hbs_hash_pair_t) * (idx + 1));
            node->subexpr.hash_pairs[idx].key = strdup(key_tok->value);
            node->subexpr.hash_pairs[idx].value = val;
            node->subexpr.hash_count++;
        }
    }

    expect(p, HBS_TOK_CLOSE_PAREN);
    return node;
}

/* Parse hash arguments into arrays, returns count */
static int parse_hash_args(hbs_parser_t *p, hbs_hash_pair_t **pairs) {
    int count = 0;
    while (is_hash_arg(p)) {
        hbs_token_t *key_tok = consume(p);
        consume(p); /* = */
        hbs_ast_node_t *val = parse_expression(p);
        if (val) {
            *pairs = realloc(*pairs, sizeof(hbs_hash_pair_t) * (count + 1));
            (*pairs)[count].key = strdup(key_tok->value);
            (*pairs)[count].value = val;
            count++;
        }
    }
    return count;
}

/* Parse positional parameters into array, returns count */
static int parse_params(hbs_parser_t *p, hbs_ast_node_t ***params,
                        hbs_token_type_t close1, hbs_token_type_t close2) {
    int count = 0;
    while (!check(p, close1) && !check(p, close2) &&
           !check(p, HBS_TOK_STRIP) && !check(p, HBS_TOK_AS) &&
           !check(p, HBS_TOK_EOF) && !is_hash_arg(p) &&
           is_expression_start(p)) {
        hbs_ast_node_t *param = parse_expression(p);
        if (param) {
            *params = realloc(*params, sizeof(hbs_ast_node_t *) * (count + 1));
            (*params)[count++] = param;
        }
    }
    return count;
}

/* ---- Mustache parsing ---- */

static hbs_ast_node_t *parse_mustache(hbs_parser_t *p, bool escaped, hbs_token_type_t close_tok) {
    bool strip_left = false;
    if (check(p, HBS_TOK_STRIP)) {
        strip_left = true;
        consume(p);
    }

    hbs_path_t *path = parse_path(p);
    hbs_ast_node_t *node = hbs_ast_mustache(path, escaped);
    node->mustache.strip_left = strip_left;

    /* Parse parameters */
    node->mustache.param_count = parse_params(p, &node->mustache.params,
                                               close_tok, close_tok);

    /* Parse hash arguments */
    node->mustache.hash_count = parse_hash_args(p, &node->mustache.hash_pairs);

    /* Strip before close */
    if (check(p, HBS_TOK_STRIP)) {
        node->mustache.strip_right = true;
        consume(p);
    }

    /* Consume close */
    expect(p, close_tok);

    return node;
}

/* ---- Block parsing ---- */

static void parse_block_params(hbs_parser_t *p, hbs_ast_node_t *node) {
    if (check(p, HBS_TOK_AS)) {
        consume(p); /* as */
        expect(p, HBS_TOK_PIPE); /* | */
        while (check(p, HBS_TOK_ID)) {
            hbs_token_t *bp = consume(p);
            node->block.block_params = realloc(node->block.block_params,
                sizeof(char *) * (node->block.block_param_count + 1));
            node->block.block_params[node->block.block_param_count++] = strdup(bp->value);
        }
        expect(p, HBS_TOK_PIPE); /* | */
    }
}

static hbs_ast_node_t *parse_block(hbs_parser_t *p) {
    bool strip_open_left = false;
    if (check(p, HBS_TOK_STRIP)) {
        strip_open_left = true;
        consume(p);
    }

    hbs_path_t *path = parse_path(p);
    hbs_ast_node_t *node = hbs_ast_block(path);
    node->block.strip_open_left = strip_open_left;

    /* Parse parameters */
    node->block.param_count = parse_params(p, &node->block.params,
                                            HBS_TOK_CLOSE, HBS_TOK_CLOSE);

    /* Parse hash arguments */
    node->block.hash_count = parse_hash_args(p, &node->block.hash_pairs);

    /* Parse block parameters: as |x y| */
    parse_block_params(p, node);

    /* Strip before close */
    if (check(p, HBS_TOK_STRIP)) {
        node->block.strip_open_right = true;
        consume(p);
    }

    expect(p, HBS_TOK_CLOSE);

    /* Parse body */
    node->block.body = parse_program(p, false);

    /* Check for {{else}} or {{^}} */
    if (check(p, HBS_TOK_OPEN_INVERSE) || (check(p, HBS_TOK_OPEN) && peek_token(p, 1) &&
        peek_token(p, 1)->type == HBS_TOK_ID && peek_token(p, 1)->value &&
        strcmp(peek_token(p, 1)->value, "else") == 0)) {

        if (check(p, HBS_TOK_OPEN_INVERSE)) {
            consume(p); /* {{^ */
        } else {
            consume(p); /* {{ */
            consume(p); /* else */
        }

        /* Check for chained else if: {{else if condition}} */
        if (check(p, HBS_TOK_ID) && current(p)->value &&
            (strcmp(current(p)->value, "if") == 0 ||
             strcmp(current(p)->value, "unless") == 0 ||
             strcmp(current(p)->value, "each") == 0 ||
             strcmp(current(p)->value, "with") == 0)) {
            hbs_ast_node_t *inverse_prog = hbs_ast_program();
            hbs_ast_node_t *nested = parse_block(p);
            hbs_ast_program_add(inverse_prog, nested);
            node->block.inverse = inverse_prog;
            return node;
        }

        if (check(p, HBS_TOK_STRIP)) {
            consume(p);
        }
        expect(p, HBS_TOK_CLOSE);

        node->block.inverse = parse_program(p, false);
    }

    /* Consume {{/helper}} */
    if (check(p, HBS_TOK_OPEN_END_BLOCK)) {
        consume(p);
        if (check(p, HBS_TOK_STRIP)) {
            node->block.strip_close_left = true;
            consume(p);
        }
        /* Consume the helper name (may be multiple path segments) */
        while (check(p, HBS_TOK_ID) || check(p, HBS_TOK_DOT)) {
            consume(p);
        }
        if (check(p, HBS_TOK_STRIP)) {
            node->block.strip_close_right = true;
            consume(p);
        }
        expect(p, HBS_TOK_CLOSE);
    }

    return node;
}

/* ---- Raw block parsing ---- */

static hbs_ast_node_t *parse_raw_block(hbs_parser_t *p) {
    /* After OPEN_RAW_BLOCK, parse helper name */
    hbs_token_t *name_tok = NULL;
    if (check(p, HBS_TOK_ID)) {
        name_tok = consume(p);
    }

    /* Consume close raw block */
    expect(p, HBS_TOK_CLOSE_RAW_BLOCK);

    /* Next token should be TEXT (the raw content) */
    char *content = NULL;
    if (check(p, HBS_TOK_TEXT)) {
        content = current(p)->value;
        consume(p);
    }

    hbs_ast_node_t *node = hbs_ast_raw_block(
        name_tok ? name_tok->value : NULL,
        content
    );

    /* Consume the close tag: OPEN_END_BLOCK ID CLOSE_RAW_BLOCK */
    expect(p, HBS_TOK_OPEN_END_BLOCK);
    if (check(p, HBS_TOK_ID)) consume(p);
    expect(p, HBS_TOK_CLOSE_RAW_BLOCK);

    return node;
}

/* ---- Partial parsing (full) ---- */

static hbs_ast_node_t *parse_partial(hbs_parser_t *p) {
    /* Check for dynamic partial: {{> (subexpr) }} */
    hbs_ast_node_t *node;
    if (check(p, HBS_TOK_OPEN_PAREN)) {
        hbs_ast_node_t *dynamic = parse_subexpr(p);
        node = hbs_ast_partial(NULL);
        node->partial.dynamic_name = dynamic;
    } else {
        hbs_path_t *name = parse_path(p);
        node = hbs_ast_partial(name);
    }

    /* Optional context argument (a path or literal before hash args) */
    if (is_expression_start(p) && !is_hash_arg(p) &&
        !check(p, HBS_TOK_CLOSE) && !check(p, HBS_TOK_STRIP)) {
        node->partial.context = parse_expression(p);
    }

    /* Hash arguments */
    node->partial.hash_count = parse_hash_args(p, &node->partial.hash_pairs);

    /* Strip and close */
    if (check(p, HBS_TOK_STRIP)) consume(p);
    expect(p, HBS_TOK_CLOSE);

    return node;
}

/* ---- Partial block: {{#> name}}fallback{{/name}} ---- */

static hbs_ast_node_t *parse_partial_block(hbs_parser_t *p) {
    /* Parse like a block but mark it as a partial block */
    hbs_path_t *path = parse_path(p);
    hbs_ast_node_t *node = hbs_ast_block(path);
    node->block.is_partial_block = true;

    /* Optional context */
    node->block.param_count = parse_params(p, &node->block.params,
                                            HBS_TOK_CLOSE, HBS_TOK_CLOSE);

    /* Hash arguments */
    node->block.hash_count = parse_hash_args(p, &node->block.hash_pairs);

    if (check(p, HBS_TOK_STRIP)) consume(p);
    expect(p, HBS_TOK_CLOSE);

    /* Parse body (the fallback / @partial-block content) */
    node->block.body = parse_program(p, false);

    /* Consume {{/name}} */
    if (check(p, HBS_TOK_OPEN_END_BLOCK)) {
        consume(p);
        if (check(p, HBS_TOK_STRIP)) consume(p);
        while (check(p, HBS_TOK_ID) || check(p, HBS_TOK_DOT)) consume(p);
        if (check(p, HBS_TOK_STRIP)) consume(p);
        expect(p, HBS_TOK_CLOSE);
    }

    return node;
}

/* ---- Inline partial: {{#*inline "name"}}body{{/inline}} ---- */

static hbs_ast_node_t *parse_inline_partial(hbs_parser_t *p) {
    /* After OPEN_DECORATOR, expect "inline" keyword */
    if (check(p, HBS_TOK_ID) && current(p)->value &&
        strcmp(current(p)->value, "inline") == 0) {
        consume(p); /* inline */
    }

    /* Parse the partial name (should be a string literal) */
    char *name = NULL;
    if (check(p, HBS_TOK_STRING)) {
        name = current(p)->value;
        consume(p);
    }

    if (check(p, HBS_TOK_STRIP)) consume(p);
    expect(p, HBS_TOK_CLOSE);

    hbs_ast_node_t *node = hbs_ast_inline_partial(name ? name : "");

    /* Parse body */
    node->inline_partial.body = parse_program(p, false);

    /* Consume {{/inline}} */
    if (check(p, HBS_TOK_OPEN_END_BLOCK)) {
        consume(p);
        if (check(p, HBS_TOK_STRIP)) consume(p);
        if (check(p, HBS_TOK_ID)) consume(p); /* "inline" */
        if (check(p, HBS_TOK_STRIP)) consume(p);
        expect(p, HBS_TOK_CLOSE);
    }

    return node;
}

/* ---- Comment parsing ---- */

static hbs_ast_node_t *parse_comment(hbs_parser_t *p) {
    hbs_token_t *body = current(p);
    const char *text = "";
    if (body && body->type == HBS_TOK_COMMENT_BODY) {
        text = body->value;
        consume(p);
    }
    return hbs_ast_comment(text);
}

/* ---- Program parsing ---- */

static hbs_ast_node_t *parse_program(hbs_parser_t *p, bool is_root) {
    hbs_ast_node_t *program = hbs_ast_program();

    while (p->pos < p->token_count) {
        hbs_token_t *tok = current(p);
        if (!tok || tok->type == HBS_TOK_EOF) break;

        /* Stop parsing body when we hit {{else}}, {{^}}, or {{/...}} */
        if (!is_root) {
            if (tok->type == HBS_TOK_OPEN_END_BLOCK ||
                tok->type == HBS_TOK_OPEN_INVERSE) {
                break;
            }
            if (tok->type == HBS_TOK_OPEN) {
                hbs_token_t *next = peek_token(p, 1);
                if (next && next->type == HBS_TOK_ID && next->value &&
                    strcmp(next->value, "else") == 0) {
                    break;
                }
            }
        }

        switch (tok->type) {
            case HBS_TOK_TEXT:
                hbs_ast_program_add(program, hbs_ast_text(tok->value));
                consume(p);
                break;

            case HBS_TOK_OPEN:
                consume(p);
                hbs_ast_program_add(program, parse_mustache(p, true, HBS_TOK_CLOSE));
                break;

            case HBS_TOK_OPEN_UNESC:
                consume(p);
                hbs_ast_program_add(program, parse_mustache(p, false, HBS_TOK_CLOSE_UNESC));
                break;

            case HBS_TOK_OPEN_UNESC_AMP:
                consume(p);
                hbs_ast_program_add(program, parse_mustache(p, false, HBS_TOK_CLOSE));
                break;

            case HBS_TOK_OPEN_BLOCK:
                consume(p);
                hbs_ast_program_add(program, parse_block(p));
                break;

            case HBS_TOK_OPEN_COMMENT:
                consume(p);
                hbs_ast_program_add(program, parse_comment(p));
                break;

            case HBS_TOK_OPEN_PARTIAL:
                consume(p);
                hbs_ast_program_add(program, parse_partial(p));
                break;

            case HBS_TOK_OPEN_PARTIAL_BLOCK:
                consume(p);
                hbs_ast_program_add(program, parse_partial_block(p));
                break;

            case HBS_TOK_OPEN_DECORATOR:
                consume(p);
                hbs_ast_program_add(program, parse_inline_partial(p));
                break;

            case HBS_TOK_OPEN_RAW_BLOCK:
                consume(p);
                hbs_ast_program_add(program, parse_raw_block(p));
                break;

            case HBS_TOK_OPEN_INVERSE:
                /* Standalone {{^helper}} at root — treat as inverse block */
                if (is_root) {
                    consume(p);
                    hbs_ast_node_t *block = parse_block(p);
                    /* Swap body and inverse for inverse blocks */
                    hbs_ast_node_t *temp = block->block.body;
                    block->block.body = block->block.inverse;
                    block->block.inverse = temp;
                    if (!block->block.body) {
                        block->block.body = hbs_ast_program();
                    }
                    hbs_ast_program_add(program, block);
                } else {
                    /* Should not happen — handled by block parser */
                    consume(p);
                }
                break;

            default:
                consume(p); /* skip unexpected tokens */
                break;
        }
    }

    return program;
}

void hbs_parser_init(hbs_parser_t *parser, hbs_token_t *tokens, int count) {
    memset(parser, 0, sizeof(hbs_parser_t));
    parser->tokens = tokens;
    parser->token_count = count;
}

hbs_ast_node_t *hbs_parser_parse(hbs_parser_t *parser) {
    return parse_program(parser, true);
}

void hbs_parser_free(hbs_parser_t *parser) {
    free(parser->error);
    parser->error = NULL;
}
