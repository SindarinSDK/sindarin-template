#ifndef HBS_RENDER_H
#define HBS_RENDER_H

#include "ast.h"
#include "utils.h"
#include <json-c/json.h>

/* Inline partial registration (scoped) */
typedef struct hbs_inline_partial {
    char *name;
    hbs_ast_node_t *body;
    struct hbs_inline_partial *next;
} hbs_inline_partial_t;

/* Context stack for nested scopes */
typedef struct hbs_context_frame {
    json_object *data;                  /* Current scope data */
    json_object *private_data;          /* @index, @key, @first, @last, etc. */
    bool owns_private_data;             /* Whether this frame owns (should free) private_data */
    char **block_param_names;           /* Block parameter names */
    json_object **block_param_values;   /* Block parameter values */
    bool *block_param_owned;            /* Whether each value is owned (should be freed) */
    int block_param_count;
    hbs_ast_node_t *partial_block;      /* @partial-block content */
    json_object *partial_block_context; /* Context for @partial-block */
    hbs_inline_partial_t *inline_partials; /* Scoped inline partials */
    struct hbs_context_frame *parent;
} hbs_context_frame_t;

/* Forward declaration of env */
typedef struct hbs_env hbs_env_t;

/* Render state */
typedef struct {
    hbs_env_t *env;
    hbs_context_frame_t *frame;         /* Top of context stack */
    hbs_strbuf_t output;
    bool has_error;                     /* Set when strict mode encounters missing var */
    char error_msg[256];                /* Error message buffer */
} hbs_render_state_t;

/* Render an AST into a string (caller must free result) */
char *hbs_render_ast(hbs_env_t *env, hbs_ast_node_t *ast, json_object *context);

#endif /* HBS_RENDER_H */
