#include "handlebars.h"
#include "test.h"

/* Custom helper: bold */
static char *helper_bold(json_object **params, int param_count, hbs_options_t *options) {
    if (param_count > 0 && params[0]) {
        const char *text = json_object_get_string(params[0]);
        size_t len = strlen(text) + 8;
        char *result = malloc(len);
        snprintf(result, len, "<b>%s</b>", text);
        return result;
    }
    return strdup("");
}

/* Custom block helper: noop (renders body unchanged) */
static char *helper_noop(json_object **params, int param_count, hbs_options_t *options) {
    if (options->fn) {
        return options->fn(options->context);
    }
    return strdup("");
}

/* Custom block helper: list (wraps each in <li>) */
static char *helper_list(json_object **params, int param_count, hbs_options_t *options) {
    if (param_count == 0 || !params[0]) return strdup("");

    json_object *items = params[0];
    if (!json_object_is_type(items, json_type_array)) return strdup("");

    size_t buf_size = 256;
    char *buf = malloc(buf_size);
    strcpy(buf, "<ul>");
    size_t pos = 4;

    int len = json_object_array_length(items);
    for (int i = 0; i < len; i++) {
        json_object *item = json_object_array_get_idx(items, i);
        if (options->fn) {
            char *rendered = options->fn(item);
            if (rendered) {
                size_t rlen = strlen(rendered);
                while (pos + rlen + 10 >= buf_size) {
                    buf_size *= 2;
                    buf = realloc(buf, buf_size);
                }
                strcat(buf + pos, "<li>");
                pos += 4;
                memcpy(buf + pos, rendered, rlen);
                pos += rlen;
                strcat(buf + pos, "</li>");
                pos += 5;
                free(rendered);
            }
        }
    }

    while (pos + 6 >= buf_size) {
        buf_size *= 2;
        buf = realloc(buf, buf_size);
    }
    strcat(buf + pos, "</ul>");
    return buf;
}

/* helperMissing hook: returns a placeholder string */
static char *helper_missing(json_object **params, int param_count, hbs_options_t *options) {
    return strdup("MISSING");
}

/* blockHelperMissing hook: renders inverse block */
static char *block_helper_missing(json_object **params, int param_count, hbs_options_t *options) {
    if (options->inverse) {
        return options->inverse(options->context);
    }
    return strdup("BLOCK_MISSING");
}

void test_helpers(void) {
    TEST_SUITE("Built-in Helpers");

    hbs_env_t *env = hbs_env_create();

    /* {{#if}} with truthy value */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "show", json_object_new_boolean(1));

        char *result = render_template(env, "{{#if show}}visible{{/if}}", ctx);
        ASSERT_STR_EQ("visible", result, "#if with truthy boolean");
        free(result);
        json_object_put(ctx);
    }

    /* {{#if}} with falsy value */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "show", json_object_new_boolean(0));

        char *result = render_template(env, "{{#if show}}visible{{/if}}", ctx);
        ASSERT_STR_EQ("", result, "#if with falsy boolean");
        free(result);
        json_object_put(ctx);
    }

    /* {{#if}} with else */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "show", json_object_new_boolean(0));

        char *result = render_template(env, "{{#if show}}yes{{else}}no{{/if}}", ctx);
        ASSERT_STR_EQ("no", result, "#if with else clause");
        free(result);
        json_object_put(ctx);
    }

    /* {{#if}} falsy: null */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "val", NULL);

        char *result = render_template(env, "{{#if val}}yes{{else}}no{{/if}}", ctx);
        ASSERT_STR_EQ("no", result, "#if null is falsy");
        free(result);
        json_object_put(ctx);
    }

    /* {{#if}} falsy: empty string */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "val", json_object_new_string(""));

        char *result = render_template(env, "{{#if val}}yes{{else}}no{{/if}}", ctx);
        ASSERT_STR_EQ("no", result, "#if empty string is falsy");
        free(result);
        json_object_put(ctx);
    }

    /* {{#if}} falsy: zero */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "val", json_object_new_int(0));

        char *result = render_template(env, "{{#if val}}yes{{else}}no{{/if}}", ctx);
        ASSERT_STR_EQ("no", result, "#if zero is falsy");
        free(result);
        json_object_put(ctx);
    }

    /* {{#if}} falsy: empty array */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "val", json_object_new_array());

        char *result = render_template(env, "{{#if val}}yes{{else}}no{{/if}}", ctx);
        ASSERT_STR_EQ("no", result, "#if empty array is falsy");
        free(result);
        json_object_put(ctx);
    }

    /* {{#if}} includeZero=true */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "val", json_object_new_int(0));

        char *result = render_template(env, "{{#if val includeZero=true}}yes{{else}}no{{/if}}", ctx);
        ASSERT_STR_EQ("yes", result, "#if includeZero=true treats 0 as truthy");
        free(result);
        json_object_put(ctx);
    }

    /* {{#unless}} */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "license", json_object_new_boolean(0));

        char *result = render_template(env, "{{#unless license}}No license{{/unless}}", ctx);
        ASSERT_STR_EQ("No license", result, "#unless with falsy value");
        free(result);
        json_object_put(ctx);
    }

    /* {{#unless}} with else */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "license", json_object_new_boolean(1));

        char *result = render_template(env, "{{#unless license}}no{{else}}yes{{/unless}}", ctx);
        ASSERT_STR_EQ("yes", result, "#unless with else clause");
        free(result);
        json_object_put(ctx);
    }

    /* Chained {{else if}} */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "a", json_object_new_boolean(0));
        json_object_object_add(ctx, "b", json_object_new_boolean(1));

        char *result = render_template(env, "{{#if a}}A{{else if b}}B{{else}}C{{/if}}", ctx);
        ASSERT_STR_EQ("B", result, "chained else if");
        free(result);
        json_object_put(ctx);
    }

    /* Chained {{else if}} - third branch */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "a", json_object_new_boolean(0));
        json_object_object_add(ctx, "b", json_object_new_boolean(0));

        char *result = render_template(env, "{{#if a}}A{{else if b}}B{{else}}C{{/if}}", ctx);
        ASSERT_STR_EQ("C", result, "chained else if - falls to else");
        free(result);
        json_object_put(ctx);
    }

    /* {{#each}} with array */
    {
        json_object *ctx = json_object_new_object();
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("a"));
        json_object_array_add(items, json_object_new_string("b"));
        json_object_array_add(items, json_object_new_string("c"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env, "{{#each items}}{{this}}{{/each}}", ctx);
        ASSERT_STR_EQ("abc", result, "#each iterates array");
        free(result);
        json_object_put(ctx);
    }

    /* {{#each}} with @index */
    {
        json_object *ctx = json_object_new_object();
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("x"));
        json_object_array_add(items, json_object_new_string("y"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env, "{{#each items}}{{@index}}:{{this}} {{/each}}", ctx);
        ASSERT_STR_EQ("0:x 1:y ", result, "#each with @index");
        free(result);
        json_object_put(ctx);
    }

    /* {{#each}} with empty array and else */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "items", json_object_new_array());

        char *result = render_template(env, "{{#each items}}item{{else}}empty{{/each}}", ctx);
        ASSERT_STR_EQ("empty", result, "#each empty array triggers else");
        free(result);
        json_object_put(ctx);
    }

    /* {{#each}} with objects */
    {
        json_object *ctx = json_object_new_object();
        json_object *items = json_object_new_array();

        json_object *item1 = json_object_new_object();
        json_object_object_add(item1, "name", json_object_new_string("Alice"));
        json_object_array_add(items, item1);

        json_object *item2 = json_object_new_object();
        json_object_object_add(item2, "name", json_object_new_string("Bob"));
        json_object_array_add(items, item2);

        json_object_object_add(ctx, "people", items);

        char *result = render_template(env, "{{#each people}}{{name}} {{/each}}", ctx);
        ASSERT_STR_EQ("Alice Bob ", result, "#each with object properties");
        free(result);
        json_object_put(ctx);
    }

    /* {{#with}} */
    {
        json_object *ctx = json_object_new_object();
        json_object *person = json_object_new_object();
        json_object_object_add(person, "name", json_object_new_string("Alice"));
        json_object_object_add(ctx, "person", person);

        char *result = render_template(env, "{{#with person}}{{name}}{{/with}}", ctx);
        ASSERT_STR_EQ("Alice", result, "#with changes context");
        free(result);
        json_object_put(ctx);
    }

    /* {{#with}} else clause */
    {
        json_object *ctx = json_object_new_object();

        char *result = render_template(env, "{{#with missing}}found{{else}}not found{{/with}}", ctx);
        ASSERT_STR_EQ("not found", result, "#with else for missing context");
        free(result);
        json_object_put(ctx);
    }

    /* lookup helper */
    {
        json_object *ctx = json_object_new_object();
        json_object *arr = json_object_new_array();
        json_object_array_add(arr, json_object_new_string("zero"));
        json_object_array_add(arr, json_object_new_string("one"));
        json_object_array_add(arr, json_object_new_string("two"));
        json_object_object_add(ctx, "items", arr);
        json_object_object_add(ctx, "idx", json_object_new_int(1));

        char *result = render_template(env, "{{lookup items idx}}", ctx);
        ASSERT_STR_EQ("one", result, "lookup helper with array and index");
        free(result);
        json_object_put(ctx);
    }

    /* Custom inline helper */
    {
        hbs_env_t *env2 = hbs_env_create();
        hbs_register_helper(env2, "bold", helper_bold);

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("World"));

        char *result = render_template(env2, "{{bold name}}", ctx);
        ASSERT_STR_EQ("<b>World</b>", result, "custom inline helper");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env2);
    }

    /* Custom block helper: noop */
    {
        hbs_env_t *env2 = hbs_env_create();
        hbs_register_helper(env2, "noop", helper_noop);

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("Alice"));

        char *result = render_template(env2, "{{#noop}}Hello {{name}}{{/noop}}", ctx);
        ASSERT_STR_EQ("Hello Alice", result, "custom block helper (noop)");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env2);
    }

    /* Custom block helper: list */
    {
        hbs_env_t *env2 = hbs_env_create();
        hbs_register_helper(env2, "list", helper_list);

        json_object *ctx = json_object_new_object();
        json_object *items = json_object_new_array();

        json_object *i1 = json_object_new_object();
        json_object_object_add(i1, "name", json_object_new_string("a"));
        json_object_array_add(items, i1);

        json_object *i2 = json_object_new_object();
        json_object_object_add(i2, "name", json_object_new_string("b"));
        json_object_array_add(items, i2);

        json_object_object_add(ctx, "items", items);

        char *result = render_template(env2, "{{#list items}}{{name}}{{/list}}", ctx);
        ASSERT_STR_EQ("<ul><li>a</li><li>b</li></ul>", result, "custom block helper (list)");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env2);
    }

    hbs_env_destroy(env);

    /* helperMissing hook */
    {
        hbs_env_t *env2 = hbs_env_create();
        hbs_register_helper(env2, "helperMissing", helper_missing);

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "x", json_object_new_string("val"));

        char *result = render_template(env2, "{{noSuchHelper x}}", ctx);
        ASSERT_STR_EQ("MISSING", result, "helperMissing hook invoked for missing helper with params");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env2);
    }

    /* helperMissing does not fire for simple path expressions */
    {
        hbs_env_t *env2 = hbs_env_create();
        hbs_register_helper(env2, "helperMissing", helper_missing);

        json_object *ctx = json_object_new_object();

        char *result = render_template(env2, "{{noSuchVar}}", ctx);
        ASSERT_STR_EQ("", result, "helperMissing does NOT fire for simple path (no params)");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env2);
    }

    /* blockHelperMissing hook */
    {
        hbs_env_t *env2 = hbs_env_create();
        hbs_register_helper(env2, "blockHelperMissing", block_helper_missing);

        char *result = render_template(env2, "{{#noBlock}}body{{else}}inverse{{/noBlock}}", NULL);
        ASSERT_STR_EQ("inverse", result, "blockHelperMissing invoked, renders inverse");
        free(result);
        hbs_env_destroy(env2);
    }

    /* hbs_unregister_helper */
    {
        hbs_env_t *env2 = hbs_env_create();
        hbs_register_helper(env2, "bold", helper_bold);

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("X"));

        char *result = render_template(env2, "{{bold name}}", ctx);
        ASSERT_STR_EQ("<b>X</b>", result, "helper exists before unregister");
        free(result);

        hbs_error_t err = hbs_unregister_helper(env2, "bold");
        ASSERT_STR_EQ("OK", hbs_error_string(err), "unregister_helper returns OK");

        result = render_template(env2, "{{bold name}}", ctx);
        ASSERT_STR_EQ("", result, "helper gone after unregister");
        free(result);

        json_object_put(ctx);
        hbs_env_destroy(env2);
    }
}
