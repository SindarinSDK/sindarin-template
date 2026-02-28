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
    const char *name;           /* Name of the helper being invoked */
    char **block_params;        /* Block parameter names (as |x y|), or NULL */
    int block_param_count;
    char *(*fn)(json_object *ctx, void *data);      /* Render block content with context */
    char *(*inverse)(json_object *ctx, void *data); /* Render else block with context */
    void *fn_data;              /* Internal: closure data for fn (do not use) */
    void *inverse_data;         /* Internal: closure data for inverse (do not use) */
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

/* Environment options */
void hbs_env_set_no_escape(hbs_env_t *env, bool enabled);   /* Disable HTML escaping globally */
void hbs_env_set_compat(hbs_env_t *env, bool enabled);      /* Recursive field lookup */
void hbs_env_set_strict(hbs_env_t *env, bool enabled);      /* Error on missing variables */

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

/* Get the error detail message from the last failed render.
 * Returns a pointer to the template's internal error buffer (valid until next render).
 * Returns NULL if no error occurred. */
const char *hbs_render_error_message(hbs_template_t *tmpl);

/* ---- Utilities ---- */

/* Create a new private data frame (shallow copy of options->data).
 * For use in custom helpers that need to set private data variables.
 * Caller must json_object_put() the returned object when done. */
json_object *hbs_create_frame(hbs_options_t *options);

/* HTML-escape a string (caller must free result) */
char *hbs_escape_html(const char *input);

/* Get human-readable error message */
const char *hbs_error_string(hbs_error_t err);

#ifdef __cplusplus
}
#endif

#endif /* HANDLEBARS_H */
