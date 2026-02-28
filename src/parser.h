#ifndef HBS_PARSER_H
#define HBS_PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    hbs_token_t *tokens;
    int token_count;
    int pos;
    char *error;
} hbs_parser_t;

void hbs_parser_init(hbs_parser_t *parser, hbs_token_t *tokens, int count);
hbs_ast_node_t *hbs_parser_parse(hbs_parser_t *parser);
void hbs_parser_free(hbs_parser_t *parser);

#endif /* HBS_PARSER_H */
