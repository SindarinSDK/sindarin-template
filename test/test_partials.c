#include "handlebars.h"
#include "test.h"

void test_partials(void) {
    TEST_SUITE("Partials");

    /* Basic partial */
    {
        hbs_env_t *env = hbs_env_create();
        hbs_register_partial(env, "greeting", "Hello, {{name}}!");

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("World"));

        char *result = render_template(env, "{{> greeting}}", ctx);
        ASSERT_STR_EQ("Hello, World!", result, "basic partial renders");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env);
    }

    /* Partial embedded in each loop */
    {
        hbs_env_t *env = hbs_env_create();
        hbs_register_partial(env, "item", "<li>{{this}}</li>");

        json_object *ctx = json_object_new_object();
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("one"));
        json_object_array_add(items, json_object_new_string("two"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env, "<ul>{{#each items}}{{> item}}{{/each}}</ul>", ctx);
        ASSERT_STR_EQ("<ul><li>one</li><li>two</li></ul>", result, "partial inside each loop");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env);
    }

    /* Partial with context */
    {
        hbs_env_t *env = hbs_env_create();
        hbs_register_partial(env, "userinfo", "{{name}} ({{age}})");

        json_object *ctx = json_object_new_object();
        json_object *user = json_object_new_object();
        json_object_object_add(user, "name", json_object_new_string("Alice"));
        json_object_object_add(user, "age", json_object_new_int(30));
        json_object_object_add(ctx, "user", user);

        char *result = render_template(env, "{{> userinfo user}}", ctx);
        ASSERT_STR_EQ("Alice (30)", result, "partial with context argument");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env);
    }

    /* Partial with hash parameters */
    {
        hbs_env_t *env = hbs_env_create();
        hbs_register_partial(env, "greeting2", "Hello {{name}} from {{from}}!");

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("World"));

        char *result = render_template(env, "{{> greeting2 from=\"Alice\"}}", ctx);
        ASSERT_STR_EQ("Hello World from Alice!", result, "partial with hash parameters");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env);
    }

    /* Partial block (fallback when partial missing) */
    {
        hbs_env_t *env = hbs_env_create();
        /* Don't register "missing" partial */

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("World"));

        char *result = render_template(env, "{{#> missing}}Fallback: {{name}}{{/missing}}", ctx);
        ASSERT_STR_EQ("Fallback: World", result, "partial block fallback for missing partial");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env);
    }

    /* Inline partial */
    {
        hbs_env_t *env = hbs_env_create();

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("World"));

        char *result = render_template(env,
            "{{#*inline \"myPartial\"}}Hello {{name}}!{{/inline}}{{> myPartial}}", ctx);
        ASSERT_STR_EQ("Hello World!", result, "inline partial definition and usage");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env);
    }

    /* Dynamic partial */
    {
        hbs_env_t *env = hbs_env_create();
        hbs_register_partial(env, "greeting", "Hello!");
        hbs_register_partial(env, "farewell", "Goodbye!");

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "which", json_object_new_string("greeting"));

        char *result = render_template(env, "{{> (lookup . \"which\")}}", ctx);
        ASSERT_STR_EQ("Hello!", result, "dynamic partial via lookup subexpression");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env);
    }

    /* Nested partials */
    {
        hbs_env_t *env = hbs_env_create();
        hbs_register_partial(env, "inner", "[{{val}}]");
        hbs_register_partial(env, "outer", "{{> inner}}");

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "val", json_object_new_string("deep"));

        char *result = render_template(env, "{{> outer}}", ctx);
        ASSERT_STR_EQ("[deep]", result, "nested partials (partial calls partial)");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env);
    }

    /* {{> @partial-block}} yielding from inside a partial */
    {
        hbs_env_t *env = hbs_env_create();
        hbs_register_partial(env, "layout", "<header/>{{> @partial-block}}<footer/>");

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "title", json_object_new_string("Home"));

        char *result = render_template(env, "{{#> layout}}My {{title}} Page{{/layout}}", ctx);
        ASSERT_STR_EQ("<header/>My Home Page<footer/>", result,
            "@partial-block yields caller content inside partial");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env);
    }

    /* @partial-block fallback when partial missing */
    {
        hbs_env_t *env = hbs_env_create();

        json_object *ctx = json_object_new_object();

        char *result = render_template(env, "{{#> missing}}fallback content{{/missing}}", ctx);
        ASSERT_STR_EQ("fallback content", result,
            "@partial-block fallback when partial not found");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env);
    }

    /* Partial auto-indentation */
    {
        hbs_env_t *env = hbs_env_create();
        hbs_register_partial(env, "content", "line1\nline2");

        char *result = render_template(env, "  {{> content}}\n", NULL);
        ASSERT_STR_EQ("  line1\n  line2", result,
            "standalone partial auto-indents each line");
        free(result);
        hbs_env_destroy(env);
    }

    /* Partial auto-indentation with deeper indent */
    {
        hbs_env_t *env = hbs_env_create();
        hbs_register_partial(env, "item", "a\nb\nc\n");

        char *result = render_template(env, "begin\n    {{> item}}\nend", NULL);
        ASSERT_STR_EQ("begin\n    a\n    b\n    c\nend", result,
            "partial auto-indentation preserves 4-space indent");
        free(result);
        hbs_env_destroy(env);
    }

    /* Parent context access: ../ inside partial called from #each */
    {
        hbs_env_t *env = hbs_env_create();
        hbs_register_partial(env, "item_p", "{{../name}}:{{this}}");

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("PARENT"));
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("a"));
        json_object_array_add(items, json_object_new_string("b"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env,
            "{{#each items}}[{{> item_p}}]{{/each}}", ctx);
        ASSERT_STR_EQ("[PARENT:a][PARENT:b]", result,
            "../ inside partial accesses each-parent context");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env);
    }

    /* @root inside partial called from #each */
    {
        hbs_env_t *env = hbs_env_create();
        hbs_register_partial(env, "root_p", "{{@root.name}}:{{this}}");

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("ROOT"));
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("x"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env,
            "{{#each items}}[{{> root_p}}]{{/each}}", ctx);
        ASSERT_STR_EQ("[ROOT:x]", result,
            "@root inside partial accesses root context");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env);
    }

    /* ../ through multiple partial boundaries (partial calls partial) */
    {
        hbs_env_t *env = hbs_env_create();
        hbs_register_partial(env, "inner_p", "{{../../name}}:{{this}}");
        hbs_register_partial(env, "outer_p", "{{> inner_p}}");

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("TOP"));
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("v"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env,
            "{{#each items}}{{> outer_p}}{{/each}}", ctx);
        ASSERT_STR_EQ("TOP:v", result,
            "../../ through nested partials accesses grandparent context");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env);
    }

    /* @root through multiple partial boundaries */
    {
        hbs_env_t *env = hbs_env_create();
        hbs_register_partial(env, "deep_root", "{{@root.title}}");
        hbs_register_partial(env, "mid_p", "{{> deep_root}}");

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "title", json_object_new_string("TITLE"));
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("z"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env,
            "{{#each items}}{{> mid_p}}{{/each}}", ctx);
        ASSERT_STR_EQ("TITLE", result,
            "@root through nested partials accesses root context");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env);
    }

    /* hbs_unregister_partial */
    {
        hbs_env_t *env = hbs_env_create();
        hbs_register_partial(env, "greet", "Hello!");

        char *result = render_template(env, "{{> greet}}", NULL);
        ASSERT_STR_EQ("Hello!", result, "partial exists before unregister");
        free(result);

        hbs_error_t err = hbs_unregister_partial(env, "greet");
        ASSERT_STR_EQ("OK", hbs_error_string(err), "unregister_partial returns OK");

        result = render_template(env, "{{> greet}}", NULL);
        ASSERT_STR_EQ("", result, "partial gone after unregister");
        free(result);

        hbs_env_destroy(env);
    }
}
