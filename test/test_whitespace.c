#include "handlebars.h"
#include "test.h"

void test_whitespace(void) {
    TEST_SUITE("Whitespace Control");

    hbs_env_t *env = hbs_env_create();

    /* ---- Standalone stripping: block open/close ---- */

    /* Standalone #if strips the tag line */
    {
        char *result = render_template(env,
            "begin\n{{#if true}}\ncontent\n{{/if}}\nend",
            json_object_new_object());
        ASSERT_STR_EQ("begin\ncontent\nend", result,
            "standalone #if/#endif strips tag lines");
        free(result);
    }

    /* Standalone #if with else strips all tag lines */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "show", json_object_new_boolean(0));
        char *result = render_template(env,
            "begin\n{{#if show}}\nyes\n{{else}}\nno\n{{/if}}\nend", ctx);
        ASSERT_STR_EQ("begin\nno\nend", result,
            "standalone if/else/endif strips all tag lines");
        free(result);
        json_object_put(ctx);
    }

    /* Standalone #each strips tag lines, preserves body newlines */
    {
        json_object *ctx = json_object_new_object();
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("a"));
        json_object_array_add(items, json_object_new_string("b"));
        json_object_array_add(items, json_object_new_string("c"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env,
            "begin\n{{#each items}}\n    item={{this}}\n{{/each}}\nend", ctx);
        ASSERT_STR_EQ("begin\n    item=a\n    item=b\n    item=c\nend", result,
            "standalone #each preserves body newlines across iterations");
        free(result);
        json_object_put(ctx);
    }

    /* Standalone comment strips entire line */
    {
        char *result = render_template(env,
            "begin\n  {{! standalone comment }}\nend", NULL);
        ASSERT_STR_EQ("begin\nend", result,
            "standalone comment strips entire line");
        free(result);
    }

    /* Inline block (not standalone) does not strip */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "x", json_object_new_boolean(1));
        char *result = render_template(env, "A{{#if x}}B{{/if}}C", ctx);
        ASSERT_STR_EQ("ABC", result,
            "inline block does not strip");
        free(result);
        json_object_put(ctx);
    }

    /* Standalone partial with indentation */
    {
        hbs_env_t *env2 = hbs_env_create();
        hbs_register_partial(env2, "p", "partial\n");
        char *result = render_template(env2, "begin\n  {{> p}}\nend", NULL);
        ASSERT_STR_EQ("begin\n  partial\nend", result,
            "standalone partial preserves indent and strips tag line");
        free(result);
        hbs_env_destroy(env2);
    }

    /* ---- Tilde (~) whitespace control on mustache ---- */

    /* Strip left: {{~expr}} */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("X"));
        char *result = render_template(env, "Hello  {{~name}}  world", ctx);
        ASSERT_STR_EQ("HelloX  world", result,
            "tilde strip left on mustache");
        free(result);
        json_object_put(ctx);
    }

    /* Strip right: {{expr~}} */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("X"));
        char *result = render_template(env, "Hello  {{name~}}  world", ctx);
        ASSERT_STR_EQ("Hello  Xworld", result,
            "tilde strip right on mustache");
        free(result);
        json_object_put(ctx);
    }

    /* Strip both: {{~expr~}} */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("X"));
        char *result = render_template(env, "Hello  {{~name~}}  world", ctx);
        ASSERT_STR_EQ("HelloXworld", result,
            "tilde strip both on mustache");
        free(result);
        json_object_put(ctx);
    }

    /* ---- Tilde (~) whitespace control on block helpers ---- */

    /* {{~#each}} strips whitespace before open tag */
    {
        json_object *ctx = json_object_new_object();
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("a"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env,
            "H\n{{~#each items}}\n  {{this}}\n{{/each}}\nF", ctx);
        ASSERT_STR_EQ("H  a\nF", result,
            "tilde on block open left strips before open tag");
        free(result);
        json_object_put(ctx);
    }

    /* {{#each~}} strips whitespace after open tag (body leading ws) */
    {
        json_object *ctx = json_object_new_object();
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("a"));
        json_object_array_add(items, json_object_new_string("b"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env,
            "H\n{{#each items~}}\n  {{this}}\n{{/each}}\nF", ctx);
        ASSERT_STR_EQ("H\na\nb\nF", result,
            "tilde on block open right strips body leading ws");
        free(result);
        json_object_put(ctx);
    }

    /* {{~/each}} strips whitespace before close tag (body trailing ws) */
    {
        json_object *ctx = json_object_new_object();
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("a"));
        json_object_array_add(items, json_object_new_string("b"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env,
            "H\n{{#each items}}\n  {{this}}\n{{~/each}}\nF", ctx);
        ASSERT_STR_EQ("H\n  a  bF", result,
            "tilde on block close left strips body trailing ws");
        free(result);
        json_object_put(ctx);
    }

    /* {{/each~}} strips whitespace after close tag */
    {
        json_object *ctx = json_object_new_object();
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("a"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env,
            "H\n{{#each items}}\n  {{this}}\n{{/each~}}\nF", ctx);
        ASSERT_STR_EQ("H\n  a\nF", result,
            "tilde on block close right strips after close tag");
        free(result);
        json_object_put(ctx);
    }

    /* All four tildes: {{~#each~}}...{{~/each~}} */
    {
        json_object *ctx = json_object_new_object();
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("a"));
        json_object_array_add(items, json_object_new_string("b"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env,
            "H\n{{~#each items~}}\n  {{this}}\n{{~/each~}}\nF", ctx);
        ASSERT_STR_EQ("HabF", result,
            "all four tildes strip all surrounding whitespace");
        free(result);
        json_object_put(ctx);
    }

    /* Tilde on #if block */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "x", json_object_new_boolean(1));
        char *result = render_template(env,
            "A  {{~#if x~}}  B  {{~/if~}}  C", ctx);
        ASSERT_STR_EQ("ABC", result,
            "tilde on #if strips all whitespace");
        free(result);
        json_object_put(ctx);
    }

    /* ---- Edge cases ---- */

    /* Empty each loop with standalone */
    {
        json_object *ctx = json_object_new_object();
        json_object *items = json_object_new_array();
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env,
            "begin\n{{#each items}}\n  item\n{{/each}}\nend", ctx);
        ASSERT_STR_EQ("begin\nend", result,
            "empty each loop with standalone tags");
        free(result);
        json_object_put(ctx);
    }

    /* Nested standalone blocks */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "a", json_object_new_boolean(1));
        json_object_object_add(ctx, "b", json_object_new_boolean(1));
        char *result = render_template(env,
            "begin\n{{#if a}}\n{{#if b}}\ncontent\n{{/if}}\n{{/if}}\nend", ctx);
        ASSERT_STR_EQ("begin\ncontent\nend", result,
            "nested standalone blocks strip correctly");
        free(result);
        json_object_put(ctx);
    }

    /* Standalone with indentation preserved */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "x", json_object_new_boolean(1));
        char *result = render_template(env,
            "begin\n  {{#if x}}\n  content\n  {{/if}}\nend", ctx);
        ASSERT_STR_EQ("begin\n  content\nend", result,
            "standalone with indentation preserves body content");
        free(result);
        json_object_put(ctx);
    }

    /* Single-item each preserves structure */
    {
        json_object *ctx = json_object_new_object();
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("x"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env,
            "H\n{{#each items}}\n    V={{this}}\n{{/each}}\nF", ctx);
        ASSERT_STR_EQ("H\n    V=x\nF", result,
            "single-item each preserves newline structure");
        free(result);
        json_object_put(ctx);
    }

    hbs_env_destroy(env);
}
