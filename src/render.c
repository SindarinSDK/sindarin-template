#include "render.h"
#include "handlebars.h"
#include "lexer.h"
#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Portable strndup for systems that lack it (e.g. Windows/MinGW) */
#if defined(_WIN32) || defined(__MINGW32__)
static char *portable_strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *dup = (char *)malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len);
        dup[len] = '\0';
    }
    return dup;
}
#define strndup portable_strndup
#endif

/* ---- Environment struct ---- */

struct hbs_env {
    struct {
        char *name;
        hbs_helper_fn fn;
    } *helpers;
    int helper_count;

    struct {
        char *name;
        char *source;
    } *partials;
    int partial_count;

    bool no_escape;     /* Disable HTML escaping for all {{expr}} */
    bool compat;        /* Enable recursive field lookup (compat mode) */
    bool strict;        /* Error on missing variables */
};

/* Template struct */
struct hbs_template {
    hbs_ast_node_t *ast;
    hbs_env_t *env;
    char last_error[256];       /* Error detail from last render */
};

/* ---- Context frame management ---- */

static hbs_context_frame_t *frame_push(hbs_context_frame_t *parent, json_object *data) {
    hbs_context_frame_t *frame = calloc(1, sizeof(hbs_context_frame_t));
    frame->data = data;
    frame->parent = parent;
    return frame;
}

static hbs_context_frame_t *frame_pop(hbs_context_frame_t *frame) {
    hbs_context_frame_t *parent = frame->parent;
    if (frame->owns_private_data && frame->private_data) {
        json_object_put(frame->private_data);
    }
    /* Free owned block param values */
    if (frame->block_param_owned) {
        for (int i = 0; i < frame->block_param_count; i++) {
            if (frame->block_param_owned[i] && frame->block_param_values[i]) {
                json_object_put(frame->block_param_values[i]);
            }
        }
        free(frame->block_param_owned);
    }
    free(frame->block_param_names);
    free(frame->block_param_values);
    /* Free inline partials */
    hbs_inline_partial_t *ip = frame->inline_partials;
    while (ip) {
        hbs_inline_partial_t *next = ip->next;
        free(ip->name);
        free(ip);
        ip = next;
    }
    free(frame);
    return parent;
}

/* ---- Forward declarations ---- */

static void render_node(hbs_render_state_t *state, hbs_ast_node_t *node);
static void render_program(hbs_render_state_t *state, hbs_ast_node_t *program);
static json_object *resolve_path(hbs_render_state_t *state, hbs_path_t *path);
static json_object *evaluate_node(hbs_render_state_t *state, hbs_ast_node_t *node);
static char *render_program_to_string(hbs_render_state_t *state, hbs_ast_node_t *program,
                                      json_object *context);

/* ---- Path resolution ---- */

static json_object *resolve_path(hbs_render_state_t *state, hbs_path_t *path) {
    if (!path) return NULL;

    /* Navigate to the correct frame based on depth (../) */
    hbs_context_frame_t *frame = state->frame;
    for (int i = 0; i < path->depth && frame->parent; i++) {
        frame = frame->parent;
    }

    /* Check block parameters first */
    if (path->part_count > 0) {
        hbs_context_frame_t *search = state->frame;
        while (search) {
            for (int i = 0; i < search->block_param_count; i++) {
                if (strcmp(search->block_param_names[i], path->parts[0]) == 0) {
                    json_object *val = search->block_param_values[i];
                    for (int j = 1; j < path->part_count; j++) {
                        if (json_object_is_type(val, json_type_object)) {
                            json_object *next = NULL;
                            json_object_object_get_ex(val, path->parts[j], &next);
                            val = next;
                        } else {
                            return NULL;
                        }
                    }
                    return val;
                }
            }
            search = search->parent;
        }
    }

    /* Handle @-prefixed data variables */
    if (path->part_count > 0 && path->parts[0][0] == '@') {
        const char *var_name = path->parts[0] + 1;

        /* @root */
        if (strcmp(var_name, "root") == 0) {
            hbs_context_frame_t *root = frame;
            while (root->parent) root = root->parent;
            json_object *val = root->data;
            for (int i = 1; i < path->part_count; i++) {
                if (json_object_is_type(val, json_type_object)) {
                    json_object *next = NULL;
                    json_object_object_get_ex(val, path->parts[i], &next);
                    val = next;
                } else {
                    return NULL;
                }
            }
            return val;
        }

        /* @partial-block */
        if (strcmp(var_name, "partial-block") == 0) {
            /* Handled specially in render_partial */
            return NULL;
        }

        /* Check private data in frame stack */
        hbs_context_frame_t *search = state->frame;
        /* Handle @../index — depth prefix on data vars */
        int data_depth = path->depth;
        while (data_depth > 0 && search && search->parent) {
            search = search->parent;
            data_depth--;
        }
        while (search) {
            if (search->private_data) {
                json_object *val = NULL;
                if (json_object_object_get_ex(search->private_data, var_name, &val)) {
                    return val;
                }
            }
            search = search->parent;
        }
        return NULL;
    }

    /* "this" or "." reference */
    if (path->is_this && path->part_count == 0) {
        return frame->data;
    }

    /* Resolve path parts against context */
    json_object *cur = frame->data;
    bool resolve_failed = false;
    for (int i = 0; i < path->part_count; i++) {
        if (!cur || !json_object_is_type(cur, json_type_object)) {
            cur = NULL;
            resolve_failed = true;
            break;
        }
        json_object *next = NULL;
        json_object_object_get_ex(cur, path->parts[i], &next);
        cur = next;
    }

    /* Compat mode: if the path was not resolved and path has no explicit depth,
     * walk up parent frames looking for the field (recursive field lookup) */
    if ((resolve_failed || !cur) && state->env && state->env->compat &&
        path->depth == 0 && path->part_count > 0) {
        hbs_context_frame_t *search = frame->parent;
        while (search) {
            if (search->data && json_object_is_type(search->data, json_type_object)) {
                json_object *found = NULL;
                json_object_object_get_ex(search->data, path->parts[0], &found);
                if (found) {
                    /* Resolve remaining segments */
                    for (int i = 1; i < path->part_count; i++) {
                        if (!found || !json_object_is_type(found, json_type_object)) {
                            found = NULL;
                            break;
                        }
                        json_object *next = NULL;
                        json_object_object_get_ex(found, path->parts[i], &next);
                        found = next;
                    }
                    if (found) return found;
                }
            }
            search = search->parent;
        }
    }

    return cur;
}

/* Evaluate an AST node to a json_object value */
static json_object *evaluate_node(hbs_render_state_t *state, hbs_ast_node_t *node) {
    if (!node) return NULL;

    switch (node->type) {
        case HBS_AST_MUSTACHE:
            return resolve_path(state, node->mustache.path);

        case HBS_AST_LITERAL:
            switch (node->literal.lit_type) {
                case HBS_LIT_STRING:
                    return json_object_new_string(node->literal.value);
                case HBS_LIT_NUMBER: {
                    if (strchr(node->literal.value, '.')) {
                        return json_object_new_double(atof(node->literal.value));
                    }
                    return json_object_new_int(atoi(node->literal.value));
                }
                case HBS_LIT_BOOLEAN:
                    return json_object_new_boolean(strcmp(node->literal.value, "true") == 0);
                case HBS_LIT_NULL:
                case HBS_LIT_UNDEFINED:
                    return NULL;
            }
            return NULL;

        case HBS_AST_TEXT:
            /* Legacy: text nodes used as literal params in old code */
            return json_object_new_string(node->text.value);

        case HBS_AST_SUBEXPR:
            /* Evaluate subexpression by calling the helper */
            if (node->subexpr.path && node->subexpr.path->part_count > 0) {
                const char *helper_name = node->subexpr.path->parts[0];

                /* Built-in: lookup */
                if (strcmp(helper_name, "lookup") == 0 && node->subexpr.param_count >= 2) {
                    json_object *obj = evaluate_node(state, node->subexpr.params[0]);
                    json_object *key = evaluate_node(state, node->subexpr.params[1]);
                    bool key_owned = (node->subexpr.params[1]->type == HBS_AST_LITERAL);

                    json_object *result = NULL;
                    if (obj && key) {
                        if (json_object_is_type(obj, json_type_array)) {
                            int idx = json_object_get_int(key);
                            result = json_object_array_get_idx(obj, idx);
                        } else if (json_object_is_type(obj, json_type_object)) {
                            const char *key_str = json_object_get_string(key);
                            json_object_object_get_ex(obj, key_str, &result);
                        }
                    }
                    if (key_owned && key) json_object_put(key);
                    bool obj_owned = (node->subexpr.params[0]->type == HBS_AST_LITERAL);
                    if (obj_owned && obj) json_object_put(obj);
                    return result;
                }

                /* Check custom helpers */
                if (state->env) {
                    for (int i = 0; i < state->env->helper_count; i++) {
                        if (strcmp(state->env->helpers[i].name, helper_name) == 0) {
                            /* Resolve params */
                            json_object **params = NULL;
                            bool *param_owned = NULL;
                            if (node->subexpr.param_count > 0) {
                                params = malloc(sizeof(json_object *) * node->subexpr.param_count);
                                param_owned = malloc(sizeof(bool) * node->subexpr.param_count);
                                for (int j = 0; j < node->subexpr.param_count; j++) {
                                    params[j] = evaluate_node(state, node->subexpr.params[j]);
                                    param_owned[j] = (node->subexpr.params[j]->type == HBS_AST_LITERAL);
                                }
                            }

                            hbs_options_t opts = {0};
                            opts.context = state->frame->data;
                            /* Build hash */
                            if (node->subexpr.hash_count > 0) {
                                opts.hash = json_object_new_object();
                                for (int j = 0; j < node->subexpr.hash_count; j++) {
                                    json_object *hv = evaluate_node(state, node->subexpr.hash_pairs[j].value);
                                    json_object_object_add(opts.hash, node->subexpr.hash_pairs[j].key,
                                        hv ? json_object_get(hv) : NULL);
                                }
                            }

                            char *result_str = state->env->helpers[i].fn(params, node->subexpr.param_count, &opts);
                            json_object *result = result_str ? json_object_new_string(result_str) : NULL;
                            free(result_str);

                            if (opts.hash) json_object_put(opts.hash);
                            if (params) {
                                for (int j = 0; j < node->subexpr.param_count; j++) {
                                    if (param_owned[j] && params[j]) json_object_put(params[j]);
                                }
                                free(params);
                                free(param_owned);
                            }
                            return result;
                        }
                    }
                }
            }
            return NULL;

        default:
            return NULL;
    }
}

/* Convert a json value to string for output */
static const char *json_value_to_string(json_object *val) {
    if (!val) return "";

    switch (json_object_get_type(val)) {
        case json_type_string:
            return json_object_get_string(val);
        case json_type_int:
        case json_type_double:
        case json_type_boolean:
            return json_object_get_string(val);
        case json_type_null:
            return "";
        default:
            return json_object_to_json_string(val);
    }
}

/* Check if a value is "falsy" per Handlebars rules */
static bool is_falsy(json_object *val) {
    if (!val) return true;

    switch (json_object_get_type(val)) {
        case json_type_null:
            return true;
        case json_type_boolean:
            return !json_object_get_boolean(val);
        case json_type_int:
            return json_object_get_int(val) == 0;
        case json_type_double:
            return json_object_get_double(val) == 0.0;
        case json_type_string:
            return strlen(json_object_get_string(val)) == 0;
        case json_type_array:
            return json_object_array_length(val) == 0;
        case json_type_object:
            return false;
    }
    return true;
}

/* Find a custom helper by name */
static hbs_helper_fn find_helper(hbs_env_t *env, const char *name) {
    if (!env || !name) return NULL;
    for (int i = 0; i < env->helper_count; i++) {
        if (strcmp(env->helpers[i].name, name) == 0) {
            return env->helpers[i].fn;
        }
    }
    return NULL;
}

/* Find a partial source by name (including inline partials in scope) */
static const char *find_partial_source(hbs_render_state_t *state, const char *name) {
    /* Check inline partials first (walk up frame stack for scoping) */
    hbs_context_frame_t *frame = state->frame;
    while (frame) {
        hbs_inline_partial_t *ip = frame->inline_partials;
        while (ip) {
            if (strcmp(ip->name, name) == 0) {
                return NULL; /* Signal to use inline partial AST instead */
            }
            ip = ip->next;
        }
        frame = frame->parent;
    }

    /* Check registered partials */
    if (state->env) {
        for (int i = 0; i < state->env->partial_count; i++) {
            if (strcmp(state->env->partials[i].name, name) == 0) {
                return state->env->partials[i].source;
            }
        }
    }
    return NULL;
}

/* Find an inline partial AST by name */
static hbs_ast_node_t *find_inline_partial(hbs_render_state_t *state, const char *name) {
    hbs_context_frame_t *frame = state->frame;
    while (frame) {
        hbs_inline_partial_t *ip = frame->inline_partials;
        while (ip) {
            if (strcmp(ip->name, name) == 0) {
                return ip->body;
            }
            ip = ip->next;
        }
        frame = frame->parent;
    }
    return NULL;
}

/* Render a program node to a new string with a given context */
static char *render_program_to_string(hbs_render_state_t *state, hbs_ast_node_t *program,
                                      json_object *context) {
    if (!program) return strdup("");

    hbs_render_state_t sub = {0};
    sub.env = state->env;
    hbs_strbuf_init(&sub.output);

    hbs_context_frame_t frame = {0};
    frame.data = context;
    frame.parent = state->frame;
    sub.frame = &frame;

    render_program(&sub, program);
    return hbs_strbuf_detach(&sub.output);
}

/* ---- Whitespace stripping ---- */

static void strip_trailing_whitespace(hbs_strbuf_t *buf) {
    while (buf->len > 0 && (buf->data[buf->len - 1] == ' ' ||
           buf->data[buf->len - 1] == '\t' ||
           buf->data[buf->len - 1] == '\n' ||
           buf->data[buf->len - 1] == '\r')) {
        buf->len--;
    }
    buf->data[buf->len] = '\0';
}

static void strip_leading_whitespace_from_text(char *text) {
    if (!text) return;
    size_t i = 0;
    size_t len = strlen(text);
    while (i < len && (text[i] == ' ' || text[i] == '\t' ||
           text[i] == '\n' || text[i] == '\r')) {
        i++;
    }
    if (i > 0) {
        memmove(text, text + i, len - i + 1);
    }
}

/* ---- Built-in helpers ---- */

static void render_builtin_if(hbs_render_state_t *state, hbs_ast_node_t *node, bool invert) {
    json_object *condition = NULL;
    bool condition_owned = false;

    if (node->block.param_count > 0) {
        condition = evaluate_node(state, node->block.params[0]);
        condition_owned = (node->block.params[0]->type == HBS_AST_LITERAL ||
                          node->block.params[0]->type == HBS_AST_SUBEXPR);
    }

    /* Check includeZero hash option */
    bool include_zero = false;
    for (int i = 0; i < node->block.hash_count; i++) {
        if (strcmp(node->block.hash_pairs[i].key, "includeZero") == 0) {
            json_object *val = evaluate_node(state, node->block.hash_pairs[i].value);
            if (val && json_object_get_boolean(val)) {
                include_zero = true;
            }
            if (node->block.hash_pairs[i].value->type == HBS_AST_LITERAL && val) {
                json_object_put(val);
            }
        }
    }

    bool truthy;
    if (include_zero && condition &&
        json_object_is_type(condition, json_type_int) &&
        json_object_get_int(condition) == 0) {
        truthy = true;
    } else {
        truthy = !is_falsy(condition);
    }
    if (invert) truthy = !truthy;

    if (truthy) {
        render_program(state, node->block.body);
    } else if (node->block.inverse) {
        render_program(state, node->block.inverse);
    }

    if (condition_owned && condition) json_object_put(condition);
}

static void render_builtin_each(hbs_render_state_t *state, hbs_ast_node_t *node) {
    json_object *collection = NULL;

    if (node->block.param_count > 0) {
        collection = evaluate_node(state, node->block.params[0]);
    }

    if (!collection || is_falsy(collection)) {
        if (node->block.inverse) {
            render_program(state, node->block.inverse);
        }
        return;
    }

    if (json_object_is_type(collection, json_type_array)) {
        int len = json_object_array_length(collection);
        if (len == 0) {
            if (node->block.inverse) render_program(state, node->block.inverse);
            return;
        }

        for (int i = 0; i < len; i++) {
            json_object *item = json_object_array_get_idx(collection, i);

            hbs_context_frame_t *child = frame_push(state->frame, item);
            child->private_data = json_object_new_object();
            child->owns_private_data = true;
            json_object_object_add(child->private_data, "index", json_object_new_int(i));
            json_object_object_add(child->private_data, "first", json_object_new_boolean(i == 0));
            json_object_object_add(child->private_data, "last", json_object_new_boolean(i == len - 1));

            if (node->block.block_param_count > 0) {
                child->block_param_count = node->block.block_param_count;
                child->block_param_names = malloc(sizeof(char *) * child->block_param_count);
                child->block_param_values = malloc(sizeof(json_object *) * child->block_param_count);
                child->block_param_owned = calloc(child->block_param_count, sizeof(bool));

                child->block_param_names[0] = node->block.block_params[0];
                child->block_param_values[0] = item;
                child->block_param_owned[0] = false;

                if (node->block.block_param_count >= 2) {
                    child->block_param_names[1] = node->block.block_params[1];
                    child->block_param_values[1] = json_object_new_int(i);
                    child->block_param_owned[1] = true;
                }
            }

            state->frame = child;
            render_program(state, node->block.body);
            state->frame = frame_pop(child);
        }
    } else if (json_object_is_type(collection, json_type_object)) {
        int len = json_object_object_length(collection);
        if (len == 0) {
            if (node->block.inverse) render_program(state, node->block.inverse);
            return;
        }

        struct json_object_iterator it = json_object_iter_begin(collection);
        struct json_object_iterator end = json_object_iter_end(collection);
        int idx = 0;

        while (!json_object_iter_equal(&it, &end)) {
            const char *key = json_object_iter_peek_name(&it);
            json_object *val = json_object_iter_peek_value(&it);

            hbs_context_frame_t *child = frame_push(state->frame, val);
            child->private_data = json_object_new_object();
            child->owns_private_data = true;
            json_object_object_add(child->private_data, "key", json_object_new_string(key));
            json_object_object_add(child->private_data, "index", json_object_new_int(idx));
            json_object_object_add(child->private_data, "first", json_object_new_boolean(idx == 0));
            json_object_object_add(child->private_data, "last", json_object_new_boolean(idx == len - 1));

            if (node->block.block_param_count >= 1) {
                child->block_param_count = node->block.block_param_count;
                child->block_param_names = malloc(sizeof(char *) * child->block_param_count);
                child->block_param_values = malloc(sizeof(json_object *) * child->block_param_count);
                child->block_param_owned = calloc(child->block_param_count, sizeof(bool));

                child->block_param_names[0] = node->block.block_params[0];
                child->block_param_values[0] = val;
                child->block_param_owned[0] = false;

                if (node->block.block_param_count >= 2) {
                    child->block_param_names[1] = node->block.block_params[1];
                    child->block_param_values[1] = json_object_new_string(key);
                    child->block_param_owned[1] = true;
                }
            }

            state->frame = child;
            render_program(state, node->block.body);
            state->frame = frame_pop(child);

            json_object_iter_next(&it);
            idx++;
        }
    }
}

static void render_builtin_with(hbs_render_state_t *state, hbs_ast_node_t *node) {
    json_object *new_ctx = NULL;

    if (node->block.param_count > 0) {
        new_ctx = evaluate_node(state, node->block.params[0]);
    }

    if (!new_ctx || is_falsy(new_ctx)) {
        if (node->block.inverse) {
            render_program(state, node->block.inverse);
        }
        return;
    }

    hbs_context_frame_t *child = frame_push(state->frame, new_ctx);

    if (node->block.block_param_count >= 1) {
        child->block_param_count = node->block.block_param_count;
        child->block_param_names = malloc(sizeof(char *) * child->block_param_count);
        child->block_param_values = malloc(sizeof(json_object *) * child->block_param_count);
        child->block_param_owned = calloc(child->block_param_count, sizeof(bool));
        child->block_param_names[0] = node->block.block_params[0];
        child->block_param_values[0] = new_ctx;
        child->block_param_owned[0] = false;
    }

    state->frame = child;
    render_program(state, node->block.body);
    state->frame = frame_pop(child);
}

static void render_builtin_lookup(hbs_render_state_t *state, hbs_ast_node_t *node, bool escaped) {
    if (node->mustache.param_count < 2) return;

    json_object *obj = evaluate_node(state, node->mustache.params[0]);
    json_object *key = evaluate_node(state, node->mustache.params[1]);
    bool key_owned = (node->mustache.params[1]->type == HBS_AST_LITERAL);
    bool obj_owned = (node->mustache.params[0]->type == HBS_AST_LITERAL);

    if (!obj) {
        if (key_owned && key) json_object_put(key);
        if (obj_owned && obj) json_object_put(obj);
        return;
    }

    json_object *result = NULL;
    if (json_object_is_type(obj, json_type_array) && key) {
        int idx = json_object_get_int(key);
        result = json_object_array_get_idx(obj, idx);
    } else if (json_object_is_type(obj, json_type_object) && key) {
        const char *key_str = json_object_get_string(key);
        json_object_object_get_ex(obj, key_str, &result);
    }

    if (result) {
        const char *str = json_value_to_string(result);
        bool should_escape = escaped && !(state->env && state->env->no_escape);
        if (should_escape) {
            char *esc = hbs_html_escape(str);
            hbs_strbuf_append(&state->output, esc);
            free(esc);
        } else {
            hbs_strbuf_append(&state->output, str);
        }
    }

    if (key_owned && key) json_object_put(key);
    if (obj_owned && obj) json_object_put(obj);
}

static void render_builtin_log(hbs_render_state_t *state, hbs_ast_node_t *node) {
    /* Determine log level from hash */
    const char *level = "info";
    for (int i = 0; i < node->mustache.hash_count; i++) {
        if (strcmp(node->mustache.hash_pairs[i].key, "level") == 0) {
            json_object *lv = evaluate_node(state, node->mustache.hash_pairs[i].value);
            if (lv) {
                level = json_object_get_string(lv);
            }
        }
    }

    for (int i = 0; i < node->mustache.param_count; i++) {
        json_object *val = evaluate_node(state, node->mustache.params[i]);
        bool owned = (node->mustache.params[i]->type == HBS_AST_LITERAL);
        fprintf(stderr, "[%s] %s\n", level, json_value_to_string(val));
        if (owned && val) json_object_put(val);
    }
}

/* ---- Custom helper invocation ---- */

/* Callback: render block body with given context */
typedef struct {
    hbs_render_state_t *state;
    hbs_ast_node_t *body;
} _helper_block_ctx_t;

static char *_helper_fn_callback(json_object *ctx, void *data) {
    _helper_block_ctx_t *bctx = (_helper_block_ctx_t *)data;
    return render_program_to_string(bctx->state, bctx->body, ctx);
}

static char *_helper_inverse_callback(json_object *ctx, void *data) {
    _helper_block_ctx_t *bctx = (_helper_block_ctx_t *)data;
    return render_program_to_string(bctx->state, bctx->body, ctx);
}

/* Callback for raw block helpers: returns the raw content string */
typedef struct { char *content; } _raw_block_ctx_t;

static char *_raw_block_fn_callback(json_object *ctx, void *data) {
    _raw_block_ctx_t *rctx = (_raw_block_ctx_t *)data;
    return rctx->content ? strdup(rctx->content) : strdup("");
}

static void render_custom_helper(hbs_render_state_t *state, const char *name,
                                  hbs_helper_fn helper_fn,
                                  hbs_ast_node_t **param_nodes, int param_count,
                                  hbs_hash_pair_t *hash_pairs, int hash_count,
                                  hbs_ast_node_t *body, hbs_ast_node_t *inverse,
                                  char **block_params, int block_param_count) {
    /* Resolve params */
    json_object **params = NULL;
    bool *param_owned = NULL;
    if (param_count > 0) {
        params = malloc(sizeof(json_object *) * param_count);
        param_owned = malloc(sizeof(bool) * param_count);
        for (int i = 0; i < param_count; i++) {
            params[i] = evaluate_node(state, param_nodes[i]);
            param_owned[i] = (param_nodes[i]->type == HBS_AST_LITERAL ||
                             param_nodes[i]->type == HBS_AST_SUBEXPR);
        }
    }

    /* Build options */
    hbs_options_t opts = {0};
    opts.context = state->frame->data;
    opts.params = params;
    opts.param_count = param_count;
    opts.name = name;
    opts.block_params = block_params;
    opts.block_param_count = block_param_count;
    opts._internal = state;

    /* Build hash */
    if (hash_count > 0) {
        opts.hash = json_object_new_object();
        for (int i = 0; i < hash_count; i++) {
            json_object *hv = evaluate_node(state, hash_pairs[i].value);
            if (hv) {
                json_object_object_add(opts.hash, hash_pairs[i].key, json_object_get(hv));
            }
        }
    }

    /* Set up fn/inverse callbacks (stack-allocated, thread-safe) */
    _helper_block_ctx_t fn_closure = {0};
    _helper_block_ctx_t inverse_closure = {0};
    if (body) {
        fn_closure.state = state;
        fn_closure.body = body;
        opts.fn = _helper_fn_callback;
        opts.fn_data = &fn_closure;
    }
    if (inverse) {
        inverse_closure.state = state;
        inverse_closure.body = inverse;
        opts.inverse = _helper_inverse_callback;
        opts.inverse_data = &inverse_closure;
    }

    /* Private data */
    if (state->frame->private_data) {
        opts.data = state->frame->private_data;
    }

    char *result = helper_fn(params, param_count, &opts);
    if (result) {
        /* Custom helpers control their own output (equivalent to SafeString) */
        hbs_strbuf_append(&state->output, result);
        free(result);
    }

    if (opts.hash) json_object_put(opts.hash);
    if (params) {
        for (int i = 0; i < param_count; i++) {
            if (param_owned[i] && params[i]) json_object_put(params[i]);
        }
        free(params);
        free(param_owned);
    }
}

/* ---- Partial rendering ---- */

static void render_partial(hbs_render_state_t *state, hbs_ast_node_t *node) {
    const char *pname = NULL;

    /* Dynamic partial: evaluate subexpression to get name */
    if (node->partial.dynamic_name) {
        json_object *name_val = evaluate_node(state, node->partial.dynamic_name);
        if (name_val) {
            pname = json_object_get_string(name_val);
        }
    } else if (node->partial.name && node->partial.name->part_count > 0) {
        pname = node->partial.name->parts[0];
    }

    if (!pname) return;

    /* Special case: {{> @partial-block}} — yield to the caller's block content */
    if (strcmp(pname, "@partial-block") == 0) {
        hbs_context_frame_t *search = state->frame;
        while (search) {
            if (search->partial_block) {
                json_object *ctx = search->partial_block_context
                    ? search->partial_block_context
                    : state->frame->data;
                hbs_context_frame_t *child = frame_push(state->frame, ctx);
                state->frame = child;
                render_program(state, search->partial_block);
                state->frame = frame_pop(child);
                return;
            }
            search = search->parent;
        }
        return;
    }

    /* Determine context for the partial */
    json_object *partial_ctx = state->frame->data;
    if (node->partial.context) {
        json_object *ctx_val = evaluate_node(state, node->partial.context);
        if (ctx_val) partial_ctx = ctx_val;
    }

    char *result = NULL;

    /* Check for inline partial first */
    hbs_ast_node_t *inline_body = find_inline_partial(state, pname);
    if (inline_body) {
        hbs_context_frame_t *child = frame_push(state->frame, partial_ctx);

        /* Add hash params to context */
        if (node->partial.hash_count > 0) {
            json_object *extended = json_object_get(partial_ctx);
            if (!json_object_is_type(extended, json_type_object)) {
                extended = json_object_new_object();
            }
            for (int i = 0; i < node->partial.hash_count; i++) {
                json_object *hv = evaluate_node(state, node->partial.hash_pairs[i].value);
                if (hv) {
                    json_object_object_add(extended, node->partial.hash_pairs[i].key,
                        json_object_get(hv));
                }
            }
            child->data = extended;
        }

        state->frame = child;
        result = render_program_to_string(state, inline_body, child->data);
        state->frame = frame_pop(child);
    } else {
        /* Find registered partial */
        if (!state->env) return;
        const char *source = NULL;
        for (int i = 0; i < state->env->partial_count; i++) {
            if (strcmp(state->env->partials[i].name, pname) == 0) {
                source = state->env->partials[i].source;
                break;
            }
        }
        if (!source) return;

        /* Compile and render the partial */
        hbs_error_t err;
        hbs_template_t *ptmpl = hbs_compile(state->env, source, &err);
        if (!ptmpl) return;

        /* Create a frame for the partial with optional hash params */
        hbs_context_frame_t *child = frame_push(state->frame, partial_ctx);

        if (node->partial.hash_count > 0) {
            json_object *extended;
            if (json_object_is_type(partial_ctx, json_type_object)) {
                extended = json_object_new_object();
                json_object_object_foreach(partial_ctx, key, val) {
                    json_object_object_add(extended, key, json_object_get(val));
                }
            } else {
                extended = json_object_new_object();
            }
            for (int i = 0; i < node->partial.hash_count; i++) {
                json_object *hv = evaluate_node(state, node->partial.hash_pairs[i].value);
                if (hv) {
                    json_object_object_add(extended, node->partial.hash_pairs[i].key,
                        json_object_get(hv));
                }
            }
            child->data = extended;
        }

        state->frame = child;
        hbs_error_t rerr;
        result = hbs_render(ptmpl, child->data, &rerr);
        state->frame = frame_pop(child);
        hbs_template_destroy(ptmpl);
    }

    if (result) {
        /* Apply auto-indentation if this partial was standalone */
        if (node->partial.indent && node->partial.indent[0]) {
            const char *indent = node->partial.indent;
            hbs_strbuf_t indented;
            hbs_strbuf_init(&indented);
            hbs_strbuf_append(&indented, indent);
            for (const char *p = result; *p; p++) {
                hbs_strbuf_append_char(&indented, *p);
                if (*p == '\n' && *(p + 1)) {
                    hbs_strbuf_append(&indented, indent);
                }
            }
            hbs_strbuf_append(&state->output, indented.data);
            hbs_strbuf_free(&indented);
        } else {
            hbs_strbuf_append(&state->output, result);
        }
        free(result);
    }
}

/* Render a partial block: {{#> name}}fallback{{/name}} */
static void render_partial_block(hbs_render_state_t *state, hbs_ast_node_t *node) {
    const char *pname = NULL;
    if (node->block.path && node->block.path->part_count > 0) {
        pname = node->block.path->parts[0];
    }
    if (!pname) return;

    /* Check if partial exists */
    const char *source = NULL;
    hbs_ast_node_t *inline_body = find_inline_partial(state, pname);

    if (!inline_body && state->env) {
        for (int i = 0; i < state->env->partial_count; i++) {
            if (strcmp(state->env->partials[i].name, pname) == 0) {
                source = state->env->partials[i].source;
                break;
            }
        }
    }

    if (!inline_body && !source) {
        /* Partial not found — render fallback (the block body) */
        render_program(state, node->block.body);
        return;
    }

    /* Set up @partial-block */
    hbs_context_frame_t *child = frame_push(state->frame, state->frame->data);
    child->partial_block = node->block.body;
    child->partial_block_context = state->frame->data;

    state->frame = child;

    if (inline_body) {
        render_program(state, inline_body);
    } else if (source) {
        hbs_error_t err;
        hbs_template_t *ptmpl = hbs_compile(state->env, source, &err);
        if (ptmpl) {
            /* Render within current state to preserve frame stack (for @partial-block) */
            render_program(state, ptmpl->ast);
            hbs_template_destroy(ptmpl);
        }
    }

    state->frame = frame_pop(child);
}

/* ---- Rendering ---- */

static void render_program(hbs_render_state_t *state, hbs_ast_node_t *program) {
    if (!program || program->type != HBS_AST_PROGRAM) return;

    for (int i = 0; i < program->program.statement_count; i++) {
        if (state->has_error) return;
        hbs_ast_node_t *stmt = program->program.statements[i];

        /* Apply whitespace stripping from previous mustache/block */
        if (i > 0 && stmt->type == HBS_AST_TEXT) {
            hbs_ast_node_t *prev = program->program.statements[i - 1];
            bool should_strip = false;
            if (prev->type == HBS_AST_MUSTACHE && prev->mustache.strip_right) {
                should_strip = true;
            } else if (prev->type == HBS_AST_BLOCK) {
                if (prev->block.strip_open_right && i == 1) {
                    should_strip = true;
                }
            }
            if (should_strip) {
                /* Strip leading whitespace from this text node */
                char *copy = strdup(stmt->text.value);
                strip_leading_whitespace_from_text(copy);
                hbs_strbuf_append(&state->output, copy);
                free(copy);
                continue;
            }
        }

        /* Check if next node wants to strip trailing whitespace */
        if (stmt->type == HBS_AST_TEXT && i + 1 < program->program.statement_count) {
            hbs_ast_node_t *next = program->program.statements[i + 1];
            bool should_strip = false;
            if (next->type == HBS_AST_MUSTACHE && next->mustache.strip_left) {
                should_strip = true;
            } else if (next->type == HBS_AST_BLOCK && next->block.strip_open_left) {
                should_strip = true;
            }
            if (should_strip) {
                /* Append text but strip trailing whitespace */
                hbs_strbuf_append(&state->output, stmt->text.value);
                strip_trailing_whitespace(&state->output);
                continue;
            }
        }

        render_node(state, stmt);
    }
}

static void render_node(hbs_render_state_t *state, hbs_ast_node_t *node) {
    if (!node || state->has_error) return;

    switch (node->type) {
        case HBS_AST_PROGRAM:
            render_program(state, node);
            break;

        case HBS_AST_TEXT:
            hbs_strbuf_append(&state->output, node->text.value);
            break;

        case HBS_AST_MUSTACHE: {
            if (node->mustache.path && node->mustache.path->part_count == 1 &&
                !node->mustache.path->is_context_explicit) {
                const char *name = node->mustache.path->parts[0];

                /* Built-in helpers */
                if (strcmp(name, "lookup") == 0) {
                    render_builtin_lookup(state, node, node->mustache.escaped);
                    break;
                }
                if (strcmp(name, "log") == 0) {
                    render_builtin_log(state, node);
                    break;
                }

                /* Custom helpers */
                hbs_helper_fn hfn = find_helper(state->env, name);
                if (hfn) {
                    render_custom_helper(state, name, hfn,
                        node->mustache.params, node->mustache.param_count,
                        node->mustache.hash_pairs, node->mustache.hash_count,
                        NULL, NULL, NULL, 0);
                    break;
                }

                /* helperMissing hook: if the path has params/hash but no helper found */
                if (node->mustache.param_count > 0 || node->mustache.hash_count > 0) {
                    hbs_helper_fn missing = find_helper(state->env, "helperMissing");
                    if (missing) {
                        render_custom_helper(state, name, missing,
                            node->mustache.params, node->mustache.param_count,
                            node->mustache.hash_pairs, node->mustache.hash_count,
                            NULL, NULL, NULL, 0);
                        break;
                    }
                }
            }

            /* Simple expression: resolve path and output */
            json_object *val = resolve_path(state, node->mustache.path);
            if (!val && state->env && state->env->strict && node->mustache.path) {
                /* Build path string for error message */
                hbs_strbuf_t pathbuf;
                hbs_strbuf_init(&pathbuf);
                for (int pi = 0; pi < node->mustache.path->depth; pi++) {
                    hbs_strbuf_append(&pathbuf, "../");
                }
                for (int pi = 0; pi < node->mustache.path->part_count; pi++) {
                    if (pi > 0) hbs_strbuf_append_char(&pathbuf, '.');
                    hbs_strbuf_append(&pathbuf, node->mustache.path->parts[pi]);
                }
                snprintf(state->error_msg, sizeof(state->error_msg),
                    "\"%s\" not defined in %s",
                    pathbuf.data ? pathbuf.data : "",
                    node->mustache.path->depth > 0 ? "parent context" : "current context");
                hbs_strbuf_free(&pathbuf);
                state->has_error = true;
                break;
            }
            if (val) {
                const char *str = json_value_to_string(val);
                bool should_escape = node->mustache.escaped &&
                    !(state->env && state->env->no_escape);
                if (should_escape) {
                    char *escaped = hbs_html_escape(str);
                    hbs_strbuf_append(&state->output, escaped);
                    free(escaped);
                } else {
                    hbs_strbuf_append(&state->output, str);
                }
            }
            break;
        }

        case HBS_AST_BLOCK: {
            /* Partial blocks */
            if (node->block.is_partial_block) {
                render_partial_block(state, node);
                break;
            }

            if (!node->block.path || node->block.path->part_count == 0) break;
            const char *name = node->block.path->parts[0];

            /* Built-in block helpers */
            if (strcmp(name, "if") == 0) {
                render_builtin_if(state, node, false);
            } else if (strcmp(name, "unless") == 0) {
                render_builtin_if(state, node, true);
            } else if (strcmp(name, "each") == 0) {
                render_builtin_each(state, node);
            } else if (strcmp(name, "with") == 0) {
                render_builtin_with(state, node);
            } else {
                /* Custom block helper */
                hbs_helper_fn hfn = find_helper(state->env, name);
                if (hfn) {
                    render_custom_helper(state, name, hfn,
                        node->block.params, node->block.param_count,
                        node->block.hash_pairs, node->block.hash_count,
                        node->block.body, node->block.inverse,
                        node->block.block_params, node->block.block_param_count);
                } else {
                    /* blockHelperMissing hook */
                    hbs_helper_fn missing = find_helper(state->env, "blockHelperMissing");
                    if (missing) {
                        render_custom_helper(state, name, missing,
                            node->block.params, node->block.param_count,
                            node->block.hash_pairs, node->block.hash_count,
                            node->block.body, node->block.inverse,
                            node->block.block_params, node->block.block_param_count);
                    } else {
                        /* Unknown block helper — render body in current context */
                        render_program(state, node->block.body);
                    }
                }
            }
            break;
        }

        case HBS_AST_PARTIAL:
            render_partial(state, node);
            break;

        case HBS_AST_COMMENT:
            break;

        case HBS_AST_RAW_BLOCK: {
            const char *rname = node->raw_block.helper_name;
            hbs_helper_fn raw_hfn = rname ? find_helper(state->env, rname) : NULL;
            if (raw_hfn) {
                /* Custom raw block helper: fn() returns the raw content */
                hbs_options_t opts = {0};
                opts.context = state->frame->data;
                opts.name = rname;
                opts._internal = state;

                _raw_block_ctx_t rctx = {
                    .content = node->raw_block.content ? node->raw_block.content : ""
                };
                opts.fn = _raw_block_fn_callback;
                opts.fn_data = &rctx;

                char *result = raw_hfn(NULL, 0, &opts);
                if (result) {
                    hbs_strbuf_append(&state->output, result);
                    free(result);
                }
            } else if (node->raw_block.content) {
                hbs_strbuf_append(&state->output, node->raw_block.content);
            }
            break;
        }

        case HBS_AST_INLINE_PARTIAL: {
            /* Register inline partial in current frame */
            hbs_inline_partial_t *ip = malloc(sizeof(hbs_inline_partial_t));
            ip->name = strdup(node->inline_partial.name);
            ip->body = node->inline_partial.body;
            ip->next = state->frame->inline_partials;
            state->frame->inline_partials = ip;
            break;
        }

        case HBS_AST_SUBEXPR:
        case HBS_AST_LITERAL:
            /* These are evaluated as values, not rendered directly */
            break;
    }
}

/* ---- Standalone whitespace stripping (post-parse AST pass) ---- */

static bool is_whitespace_only(const char *text, size_t start, size_t end) {
    for (size_t i = start; i < end; i++) {
        if (text[i] != ' ' && text[i] != '\t') return false;
    }
    return true;
}

/*
 * Check if preceding text allows standalone.
 * text: the text content
 * is_first_stmt: true if this text node is the first statement of its program
 * trunc_pos: output - where to truncate the text
 *
 * Rules: prev text must have a newline, and after last \n must be all whitespace.
 * Exception: if it's the first statement in its program and entirely whitespace,
 * the tag is on the "first line" of the program — also standalone.
 */
static bool check_prev_standalone(const char *text, bool is_first_stmt, size_t *trunc_pos) {
    size_t len = strlen(text);

    /* Find last newline */
    size_t after_nl = 0;
    bool has_newline = false;
    for (size_t i = len; i > 0; i--) {
        if (text[i - 1] == '\n') {
            after_nl = i;
            has_newline = true;
            break;
        }
    }

    if (has_newline) {
        if (is_whitespace_only(text, after_nl, len)) {
            *trunc_pos = after_nl;
            return true;
        }
        return false;
    }

    /* No newline — only OK if first stmt and entirely whitespace */
    if (is_first_stmt && is_whitespace_only(text, 0, len)) {
        *trunc_pos = 0;
        return true;
    }
    return false;
}

/*
 * Check if following text allows standalone.
 * text: the text content
 * is_last_stmt: true if this text node is the last statement of its program
 * skip_pos: output - how many chars to skip from start
 */
static bool check_next_standalone(const char *text, bool is_last_stmt, size_t *skip_pos) {
    size_t len = strlen(text);

    /* Find first newline */
    size_t first_nl = len;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') {
            first_nl = i;
            break;
        }
    }

    if (first_nl < len) {
        /* Has newline — text before it must be all whitespace */
        if (is_whitespace_only(text, 0, first_nl)) {
            *skip_pos = first_nl + 1;
            return true;
        }
        return false;
    }

    /* No newline — only OK if last stmt and entirely whitespace */
    if (is_last_stmt && is_whitespace_only(text, 0, len)) {
        *skip_pos = len;
        return true;
    }
    return false;
}

static void apply_strip(hbs_ast_node_t *prev, size_t prev_trunc,
                         hbs_ast_node_t *next, size_t next_skip) {
    if (prev) {
        prev->text.value[prev_trunc] = '\0';
    }
    if (next && next_skip > 0) {
        size_t len = strlen(next->text.value);
        memmove(next->text.value, next->text.value + next_skip, len - next_skip + 1);
    }
}

/* Forward declaration for recursion */
static void standalone_strip(hbs_ast_node_t *program);

static bool is_standalone_simple(hbs_ast_node_t *node) {
    return node->type == HBS_AST_COMMENT ||
           node->type == HBS_AST_PARTIAL ||
           node->type == HBS_AST_INLINE_PARTIAL;
}

/*
 * Try standalone strip for a simple node (COMMENT/PARTIAL/INLINE_PARTIAL) at index i.
 */
static void try_strip_standalone(hbs_ast_node_t **stmts, int count, int i) {
    hbs_ast_node_t *node = stmts[i];
    hbs_ast_node_t *prev = (i > 0 && stmts[i - 1]->type == HBS_AST_TEXT) ? stmts[i - 1] : NULL;
    hbs_ast_node_t *next = (i + 1 < count && stmts[i + 1]->type == HBS_AST_TEXT) ? stmts[i + 1] : NULL;

    bool prev_ok = false;
    size_t prev_trunc = 0;
    if (!prev && i == 0) {
        prev_ok = true;
    } else if (prev) {
        prev_ok = check_prev_standalone(prev->text.value, i - 1 == 0, &prev_trunc);
    }

    bool next_ok = false;
    size_t next_skip = 0;
    if (!next && i + 1 >= count) {
        next_ok = true;
    } else if (next) {
        next_ok = check_next_standalone(next->text.value, i + 1 == count - 1, &next_skip);
    }

    if (prev_ok && next_ok) {
        /* For partials, capture the whitespace indent before stripping */
        if (node->type == HBS_AST_PARTIAL && prev && prev_trunc < strlen(prev->text.value)) {
            const char *indent_start = prev->text.value + prev_trunc;
            size_t indent_len = strlen(indent_start);
            if (indent_len > 0) {
                node->partial.indent = strndup(indent_start, indent_len);
            }
        }
        apply_strip(prev, prev_trunc, next, next_skip);
    }
}

/*
 * Try standalone strip for a BLOCK node's open tag.
 * prev = outer stmt before block, next = first stmt of body.
 */
static void try_strip_block_open(hbs_ast_node_t **outer_stmts, int outer_count, int block_idx,
                                  hbs_ast_node_t *body) {
    hbs_ast_node_t *prev = (block_idx > 0 && outer_stmts[block_idx - 1]->type == HBS_AST_TEXT)
        ? outer_stmts[block_idx - 1] : NULL;
    hbs_ast_node_t *next = NULL;
    int body_count = 0;
    if (body && body->type == HBS_AST_PROGRAM) {
        body_count = body->program.statement_count;
        if (body_count > 0 && body->program.statements[0]->type == HBS_AST_TEXT) {
            next = body->program.statements[0];
        }
    }

    bool prev_ok = false;
    size_t prev_trunc = 0;
    if (!prev && block_idx == 0) {
        prev_ok = true;
    } else if (prev) {
        prev_ok = check_prev_standalone(prev->text.value, block_idx - 1 == 0, &prev_trunc);
    }

    bool next_ok = false;
    size_t next_skip = 0;
    if (!next && body_count == 0) {
        next_ok = true;
    } else if (next) {
        next_ok = check_next_standalone(next->text.value, body_count == 1, &next_skip);
    }

    if (prev_ok && next_ok) {
        apply_strip(prev, prev_trunc, next, next_skip);
    }
}

/*
 * Try standalone strip for a BLOCK node's close tag.
 * prev = last stmt of body (or inverse), next = outer stmt after block.
 */
static void try_strip_block_close(hbs_ast_node_t **outer_stmts, int outer_count, int block_idx,
                                   hbs_ast_node_t *last_body) {
    hbs_ast_node_t *prev = NULL;
    int body_count = 0;
    if (last_body && last_body->type == HBS_AST_PROGRAM) {
        body_count = last_body->program.statement_count;
        if (body_count > 0) {
            hbs_ast_node_t *last = last_body->program.statements[body_count - 1];
            if (last->type == HBS_AST_TEXT) prev = last;
        }
    }

    hbs_ast_node_t *next = (block_idx + 1 < outer_count &&
        outer_stmts[block_idx + 1]->type == HBS_AST_TEXT)
        ? outer_stmts[block_idx + 1] : NULL;

    bool prev_ok = false;
    size_t prev_trunc = 0;
    if (!prev && body_count == 0) {
        prev_ok = true;
    } else if (prev) {
        prev_ok = check_prev_standalone(prev->text.value, body_count == 1, &prev_trunc);
    }

    bool next_ok = false;
    size_t next_skip = 0;
    if (!next && block_idx + 1 >= outer_count) {
        next_ok = true;
    } else if (next) {
        next_ok = check_next_standalone(next->text.value, block_idx + 1 == outer_count - 1, &next_skip);
    }

    if (prev_ok && next_ok) {
        apply_strip(prev, prev_trunc, next, next_skip);
    }
}

/*
 * Try standalone strip for a BLOCK node's else tag.
 * prev = last stmt of body, next = first stmt of inverse.
 */
static void try_strip_block_else(hbs_ast_node_t *body, hbs_ast_node_t *inverse) {
    hbs_ast_node_t *prev = NULL;
    int body_count = 0;
    if (body && body->type == HBS_AST_PROGRAM) {
        body_count = body->program.statement_count;
        if (body_count > 0) {
            hbs_ast_node_t *last = body->program.statements[body_count - 1];
            if (last->type == HBS_AST_TEXT) prev = last;
        }
    }

    hbs_ast_node_t *next = NULL;
    int inv_count = 0;
    if (inverse && inverse->type == HBS_AST_PROGRAM) {
        inv_count = inverse->program.statement_count;
        if (inv_count > 0 && inverse->program.statements[0]->type == HBS_AST_TEXT) {
            next = inverse->program.statements[0];
        }
    }

    bool prev_ok = false;
    size_t prev_trunc = 0;
    if (!prev && body_count == 0) {
        prev_ok = true;
    } else if (prev) {
        prev_ok = check_prev_standalone(prev->text.value, body_count == 1, &prev_trunc);
    }

    bool next_ok = false;
    size_t next_skip = 0;
    if (!next && inv_count == 0) {
        next_ok = true;
    } else if (next) {
        next_ok = check_next_standalone(next->text.value, inv_count == 1, &next_skip);
    }

    if (prev_ok && next_ok) {
        apply_strip(prev, prev_trunc, next, next_skip);
    }
}

/*
 * Post-parse AST pass: strip standalone whitespace lines for
 * comments, partials, inline partials, and block open/close/else tags.
 */
static void standalone_strip(hbs_ast_node_t *program) {
    if (!program || program->type != HBS_AST_PROGRAM) return;

    int count = program->program.statement_count;
    hbs_ast_node_t **stmts = program->program.statements;

    for (int i = 0; i < count; i++) {
        hbs_ast_node_t *node = stmts[i];

        if (is_standalone_simple(node)) {
            try_strip_standalone(stmts, count, i);
        }

        if (node->type == HBS_AST_BLOCK) {
            hbs_ast_node_t *last_body = node->block.inverse ? node->block.inverse : node->block.body;

            try_strip_block_open(stmts, count, i, node->block.body);

            if (node->block.inverse) {
                try_strip_block_else(node->block.body, node->block.inverse);
            }

            try_strip_block_close(stmts, count, i, last_body);

            /* Recurse into body and inverse */
            standalone_strip(node->block.body);
            standalone_strip(node->block.inverse);
        }
    }
}

/* ---- Public API implementation ---- */

hbs_env_t *hbs_env_create(void) {
    return calloc(1, sizeof(hbs_env_t));
}

void hbs_env_set_no_escape(hbs_env_t *env, bool enabled) {
    if (env) env->no_escape = enabled;
}

void hbs_env_set_compat(hbs_env_t *env, bool enabled) {
    if (env) env->compat = enabled;
}

void hbs_env_set_strict(hbs_env_t *env, bool enabled) {
    if (env) env->strict = enabled;
}

void hbs_env_destroy(hbs_env_t *env) {
    if (!env) return;
    for (int i = 0; i < env->helper_count; i++) {
        free(env->helpers[i].name);
    }
    free(env->helpers);
    for (int i = 0; i < env->partial_count; i++) {
        free(env->partials[i].name);
        free(env->partials[i].source);
    }
    free(env->partials);
    free(env);
}

hbs_error_t hbs_register_helper(hbs_env_t *env, const char *name, hbs_helper_fn fn) {
    if (!env || !name || !fn) return HBS_ERR_INVALID_ARG;
    env->helpers = realloc(env->helpers, sizeof(*env->helpers) * (env->helper_count + 1));
    env->helpers[env->helper_count].name = strdup(name);
    env->helpers[env->helper_count].fn = fn;
    env->helper_count++;
    return HBS_OK;
}

hbs_error_t hbs_unregister_helper(hbs_env_t *env, const char *name) {
    if (!env || !name) return HBS_ERR_INVALID_ARG;
    for (int i = 0; i < env->helper_count; i++) {
        if (strcmp(env->helpers[i].name, name) == 0) {
            free(env->helpers[i].name);
            /* Shift remaining entries */
            for (int j = i; j < env->helper_count - 1; j++) {
                env->helpers[j] = env->helpers[j + 1];
            }
            env->helper_count--;
            return HBS_OK;
        }
    }
    return HBS_ERR_NOT_FOUND;
}

hbs_error_t hbs_register_partial(hbs_env_t *env, const char *name, const char *source) {
    if (!env || !name || !source) return HBS_ERR_INVALID_ARG;
    env->partials = realloc(env->partials, sizeof(*env->partials) * (env->partial_count + 1));
    env->partials[env->partial_count].name = strdup(name);
    env->partials[env->partial_count].source = strdup(source);
    env->partial_count++;
    return HBS_OK;
}

hbs_error_t hbs_unregister_partial(hbs_env_t *env, const char *name) {
    if (!env || !name) return HBS_ERR_INVALID_ARG;
    for (int i = 0; i < env->partial_count; i++) {
        if (strcmp(env->partials[i].name, name) == 0) {
            free(env->partials[i].name);
            free(env->partials[i].source);
            for (int j = i; j < env->partial_count - 1; j++) {
                env->partials[j] = env->partials[j + 1];
            }
            env->partial_count--;
            return HBS_OK;
        }
    }
    return HBS_ERR_NOT_FOUND;
}

hbs_template_t *hbs_compile(hbs_env_t *env, const char *source, hbs_error_t *err) {
    if (!source) {
        if (err) *err = HBS_ERR_INVALID_ARG;
        return NULL;
    }

    hbs_lexer_t lexer;
    hbs_lexer_init(&lexer, source);
    if (hbs_lexer_tokenize(&lexer) != 0) {
        if (err) *err = HBS_ERR_PARSE;
        hbs_lexer_free(&lexer);
        return NULL;
    }

    hbs_parser_t parser;
    hbs_parser_init(&parser, lexer.tokens, lexer.token_count);
    hbs_ast_node_t *ast = hbs_parser_parse(&parser);

    if (parser.error) {
        if (err) *err = HBS_ERR_PARSE;
        hbs_ast_destroy(ast);
        hbs_parser_free(&parser);
        hbs_lexer_free(&lexer);
        return NULL;
    }

    /* Post-parse: strip standalone whitespace lines */
    standalone_strip(ast);

    hbs_template_t *tmpl = calloc(1, sizeof(hbs_template_t));
    tmpl->ast = ast;
    tmpl->env = env;

    hbs_parser_free(&parser);
    hbs_lexer_free(&lexer);

    if (err) *err = HBS_OK;
    return tmpl;
}

void hbs_template_destroy(hbs_template_t *tmpl) {
    if (!tmpl) return;
    hbs_ast_destroy(tmpl->ast);
    free(tmpl);
}

char *hbs_render(hbs_template_t *tmpl, json_object *context, hbs_error_t *err) {
    if (!tmpl || !tmpl->ast) {
        if (err) *err = HBS_ERR_INVALID_ARG;
        return NULL;
    }

    hbs_render_state_t state = {0};
    state.env = tmpl->env;
    hbs_strbuf_init(&state.output);

    hbs_context_frame_t root_frame = {0};
    root_frame.data = context;
    state.frame = &root_frame;

    render_program(&state, tmpl->ast);

    if (state.has_error) {
        if (err) *err = HBS_ERR_RENDER;
        memcpy(tmpl->last_error, state.error_msg, sizeof(tmpl->last_error));
        hbs_strbuf_free(&state.output);
        return NULL;
    }

    tmpl->last_error[0] = '\0';
    if (err) *err = HBS_OK;
    return hbs_strbuf_detach(&state.output);
}

const char *hbs_render_error_message(hbs_template_t *tmpl) {
    if (!tmpl || tmpl->last_error[0] == '\0') return NULL;
    return tmpl->last_error;
}

json_object *hbs_create_frame(hbs_options_t *options) {
    json_object *frame = json_object_new_object();
    if (options && options->data && json_object_is_type(options->data, json_type_object)) {
        json_object_object_foreach(options->data, key, val) {
            json_object_object_add(frame, key, val ? json_object_get(val) : NULL);
        }
    }
    return frame;
}

char *hbs_escape_html(const char *input) {
    return hbs_html_escape(input);
}

const char *hbs_error_string(hbs_error_t err) {
    switch (err) {
        case HBS_OK:              return "OK";
        case HBS_ERR_PARSE:       return "Parse error";
        case HBS_ERR_RENDER:      return "Render error";
        case HBS_ERR_MEMORY:      return "Memory allocation error";
        case HBS_ERR_NOT_FOUND:   return "Not found";
        case HBS_ERR_INVALID_ARG: return "Invalid argument";
        default:                  return "Unknown error";
    }
}
