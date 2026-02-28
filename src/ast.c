#include "ast.h"
#include <stdlib.h>
#include <string.h>

hbs_ast_node_t *hbs_ast_program(void) {
    hbs_ast_node_t *node = calloc(1, sizeof(hbs_ast_node_t));
    node->type = HBS_AST_PROGRAM;
    return node;
}

hbs_ast_node_t *hbs_ast_text(const char *value) {
    hbs_ast_node_t *node = calloc(1, sizeof(hbs_ast_node_t));
    node->type = HBS_AST_TEXT;
    node->text.value = strdup(value);
    return node;
}

hbs_ast_node_t *hbs_ast_mustache(hbs_path_t *path, bool escaped) {
    hbs_ast_node_t *node = calloc(1, sizeof(hbs_ast_node_t));
    node->type = HBS_AST_MUSTACHE;
    node->mustache.path = path;
    node->mustache.escaped = escaped;
    return node;
}

hbs_ast_node_t *hbs_ast_block(hbs_path_t *path) {
    hbs_ast_node_t *node = calloc(1, sizeof(hbs_ast_node_t));
    node->type = HBS_AST_BLOCK;
    node->block.path = path;
    return node;
}

hbs_ast_node_t *hbs_ast_comment(const char *value) {
    hbs_ast_node_t *node = calloc(1, sizeof(hbs_ast_node_t));
    node->type = HBS_AST_COMMENT;
    node->comment.value = strdup(value);
    return node;
}

hbs_ast_node_t *hbs_ast_partial(hbs_path_t *name) {
    hbs_ast_node_t *node = calloc(1, sizeof(hbs_ast_node_t));
    node->type = HBS_AST_PARTIAL;
    node->partial.name = name;
    return node;
}

hbs_ast_node_t *hbs_ast_raw_block(const char *helper_name, const char *content) {
    hbs_ast_node_t *node = calloc(1, sizeof(hbs_ast_node_t));
    node->type = HBS_AST_RAW_BLOCK;
    node->raw_block.helper_name = helper_name ? strdup(helper_name) : NULL;
    node->raw_block.content = content ? strdup(content) : NULL;
    return node;
}

hbs_ast_node_t *hbs_ast_subexpr(hbs_path_t *path) {
    hbs_ast_node_t *node = calloc(1, sizeof(hbs_ast_node_t));
    node->type = HBS_AST_SUBEXPR;
    node->subexpr.path = path;
    return node;
}

hbs_ast_node_t *hbs_ast_inline_partial(const char *name) {
    hbs_ast_node_t *node = calloc(1, sizeof(hbs_ast_node_t));
    node->type = HBS_AST_INLINE_PARTIAL;
    node->inline_partial.name = strdup(name);
    return node;
}

hbs_ast_node_t *hbs_ast_literal(hbs_literal_type_t lit_type, const char *value) {
    hbs_ast_node_t *node = calloc(1, sizeof(hbs_ast_node_t));
    node->type = HBS_AST_LITERAL;
    node->literal.lit_type = lit_type;
    node->literal.value = value ? strdup(value) : NULL;
    return node;
}

void hbs_ast_program_add(hbs_ast_node_t *program, hbs_ast_node_t *stmt) {
    int count = program->program.statement_count;
    program->program.statements = realloc(
        program->program.statements,
        sizeof(hbs_ast_node_t *) * (count + 1)
    );
    program->program.statements[count] = stmt;
    program->program.statement_count = count + 1;
}

hbs_path_t *hbs_path_create(void) {
    hbs_path_t *path = calloc(1, sizeof(hbs_path_t));
    return path;
}

void hbs_path_add_part(hbs_path_t *path, const char *part) {
    path->parts = realloc(path->parts, sizeof(char *) * (path->part_count + 1));
    path->parts[path->part_count] = strdup(part);
    path->part_count++;
}

void hbs_path_destroy(hbs_path_t *path) {
    if (!path) return;
    for (int i = 0; i < path->part_count; i++) {
        free(path->parts[i]);
    }
    free(path->parts);
    free(path);
}

void hbs_ast_destroy(hbs_ast_node_t *node) {
    if (!node) return;

    switch (node->type) {
        case HBS_AST_PROGRAM:
            for (int i = 0; i < node->program.statement_count; i++) {
                hbs_ast_destroy(node->program.statements[i]);
            }
            free(node->program.statements);
            break;

        case HBS_AST_TEXT:
            free(node->text.value);
            break;

        case HBS_AST_MUSTACHE:
            hbs_path_destroy(node->mustache.path);
            for (int i = 0; i < node->mustache.param_count; i++) {
                hbs_ast_destroy(node->mustache.params[i]);
            }
            free(node->mustache.params);
            for (int i = 0; i < node->mustache.hash_count; i++) {
                free(node->mustache.hash_pairs[i].key);
                hbs_ast_destroy(node->mustache.hash_pairs[i].value);
            }
            free(node->mustache.hash_pairs);
            break;

        case HBS_AST_BLOCK:
            hbs_path_destroy(node->block.path);
            for (int i = 0; i < node->block.param_count; i++) {
                hbs_ast_destroy(node->block.params[i]);
            }
            free(node->block.params);
            for (int i = 0; i < node->block.hash_count; i++) {
                free(node->block.hash_pairs[i].key);
                hbs_ast_destroy(node->block.hash_pairs[i].value);
            }
            free(node->block.hash_pairs);
            hbs_ast_destroy(node->block.body);
            hbs_ast_destroy(node->block.inverse);
            for (int i = 0; i < node->block.block_param_count; i++) {
                free(node->block.block_params[i]);
            }
            free(node->block.block_params);
            break;

        case HBS_AST_PARTIAL:
            hbs_path_destroy(node->partial.name);
            hbs_ast_destroy(node->partial.dynamic_name);
            hbs_ast_destroy(node->partial.context);
            for (int i = 0; i < node->partial.hash_count; i++) {
                free(node->partial.hash_pairs[i].key);
                hbs_ast_destroy(node->partial.hash_pairs[i].value);
            }
            free(node->partial.hash_pairs);
            free(node->partial.indent);
            break;

        case HBS_AST_COMMENT:
            free(node->comment.value);
            break;

        case HBS_AST_RAW_BLOCK:
            free(node->raw_block.helper_name);
            free(node->raw_block.content);
            break;

        case HBS_AST_SUBEXPR:
            hbs_path_destroy(node->subexpr.path);
            for (int i = 0; i < node->subexpr.param_count; i++) {
                hbs_ast_destroy(node->subexpr.params[i]);
            }
            free(node->subexpr.params);
            for (int i = 0; i < node->subexpr.hash_count; i++) {
                free(node->subexpr.hash_pairs[i].key);
                hbs_ast_destroy(node->subexpr.hash_pairs[i].value);
            }
            free(node->subexpr.hash_pairs);
            break;

        case HBS_AST_INLINE_PARTIAL:
            free(node->inline_partial.name);
            hbs_ast_destroy(node->inline_partial.body);
            break;

        case HBS_AST_LITERAL:
            free(node->literal.value);
            break;
    }

    free(node);
}
