#ifndef HANDLEBARS_H
#define HANDLEBARS_H

#include <json-c/json.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque types */
typedef struct hbs_template hbs_template_t;
typedef struct hbs_env hbs_env_t;

/* Helper function options (passed to custom helpers) */
typedef struct hbs_options {
    json_object *hash;          /* Hash arguments as JSON object */
    json_object *data;          /* Private data frame (@index, @key, etc.) */
    json_object *context;       /* Current context */
    json_object **params;       /* Resolved positional parameters */
    int param_count;
    char *(*fn)(json_object *ctx);       /* Render block content with context */
    char *(*inverse)(json_object *ctx);  /* Render else block with context */
    void *_internal;            /* Internal render state (do not use) */
} hbs_options_t;

/* Helper callback signature */
typedef char *(*hbs_helper_fn)(json_object **params, int param_count, hbs_options_t *options);

/* Error codes */
typedef enum {
    HBS_OK = 0,
    HBS_ERR_PARSE,
    HBS_ERR_RENDER,
    HBS_ERR_MEMORY,
    HBS_ERR_NOT_FOUND,
    HBS_ERR_INVALID_ARG
} hbs_error_t;

/* ---- Environment (holds helpers + partials) ---- */

hbs_env_t *hbs_env_create(void);
void hbs_env_destroy(hbs_env_t *env);

/* Register a custom helper */
hbs_error_t hbs_register_helper(hbs_env_t *env, const char *name, hbs_helper_fn fn);

/* Unregister a custom helper by name */
hbs_error_t hbs_unregister_helper(hbs_env_t *env, const char *name);

/* Register a partial template */
hbs_error_t hbs_register_partial(hbs_env_t *env, const char *name, const char *source);

/* Unregister a partial by name */
hbs_error_t hbs_unregister_partial(hbs_env_t *env, const char *name);

/* ---- Template compilation ---- */

hbs_template_t *hbs_compile(hbs_env_t *env, const char *source, hbs_error_t *err);
void hbs_template_destroy(hbs_template_t *tmpl);

/* ---- Rendering ---- */

char *hbs_render(hbs_template_t *tmpl, json_object *context, hbs_error_t *err);

/* ---- Utilities ---- */

/* HTML-escape a string (caller must free result) */
char *hbs_escape_html(const char *input);

/* Get human-readable error message */
const char *hbs_error_string(hbs_error_t err);

#ifdef __cplusplus
}
#endif

#endif /* HANDLEBARS_H */
