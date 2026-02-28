#include "handlebars.h"
#include "test.h"

void test_blocks(void) {
    TEST_SUITE("Block Helpers & Features");

    hbs_env_t *env = hbs_env_create();

    /* {{#each}} with block parameters */
    {
        json_object *ctx = json_object_new_object();
        json_object *users = json_object_new_array();

        json_object *u1 = json_object_new_object();
        json_object_object_add(u1, "name", json_object_new_string("Alice"));
        json_object_array_add(users, u1);

        json_object *u2 = json_object_new_object();
        json_object_object_add(u2, "name", json_object_new_string("Bob"));
        json_object_array_add(users, u2);

        json_object_object_add(ctx, "users", users);

        char *result = render_template(env,
            "{{#each users as |user idx|}}{{idx}}:{{user.name}} {{/each}}", ctx);
        ASSERT_STR_EQ("0:Alice 1:Bob ", result, "#each with block params (as |user idx|)");
        free(result);
        json_object_put(ctx);
    }

    /* @first and @last */
    {
        json_object *ctx = json_object_new_object();
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("a"));
        json_object_array_add(items, json_object_new_string("b"));
        json_object_array_add(items, json_object_new_string("c"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env,
            "{{#each items}}{{#if @first}}[{{/if}}{{this}}{{#if @last}}]{{/if}}{{/each}}", ctx);
        ASSERT_STR_EQ("[abc]", result, "@first and @last in each loop");
        free(result);
        json_object_put(ctx);
    }

    /* Nested {{#each}} */
    {
        json_object *ctx = json_object_new_object();
        json_object *groups = json_object_new_array();

        json_object *g1 = json_object_new_object();
        json_object *g1_items = json_object_new_array();
        json_object_array_add(g1_items, json_object_new_string("a"));
        json_object_array_add(g1_items, json_object_new_string("b"));
        json_object_object_add(g1, "items", g1_items);
        json_object_array_add(groups, g1);

        json_object *g2 = json_object_new_object();
        json_object *g2_items = json_object_new_array();
        json_object_array_add(g2_items, json_object_new_string("c"));
        json_object_object_add(g2, "items", g2_items);
        json_object_array_add(groups, g2);

        json_object_object_add(ctx, "groups", groups);

        char *result = render_template(env,
            "{{#each groups}}[{{#each items}}{{this}}{{/each}}]{{/each}}", ctx);
        ASSERT_STR_EQ("[ab][c]", result, "nested each loops");
        free(result);
        json_object_put(ctx);
    }

    /* Comments produce no output */
    {
        char *result = render_template(env, "before{{! this is a comment }}after", NULL);
        ASSERT_STR_EQ("beforeafter", result, "comments produce no output");
        free(result);
    }

    /* Long comments */
    {
        char *result = render_template(env,
            "before{{!-- this has {{ braces }} --}}after", NULL);
        ASSERT_STR_EQ("beforeafter", result, "long comments with braces");
        free(result);
    }

    /* {{#each}} over object keys with @key */
    {
        json_object *ctx = json_object_new_object();
        json_object *obj = json_object_new_object();
        json_object_object_add(obj, "x", json_object_new_int(1));
        json_object_object_add(obj, "y", json_object_new_int(2));
        json_object_object_add(ctx, "obj", obj);

        char *result = render_template(env,
            "{{#each obj}}{{@key}}={{this}} {{/each}}", ctx);
        ASSERT_STR_EQ("x=1 y=2 ", result, "#each over object with @key");
        free(result);
        json_object_put(ctx);
    }

    /* Raw block */
    {
        char *result = render_template(env,
            "{{{{raw}}}}{{not-processed}}{{{{/raw}}}}", NULL);
        ASSERT_STR_EQ("{{not-processed}}", result, "raw block preserves content literally");
        free(result);
    }

    /* Nested {{#each}} with @../index */
    {
        json_object *ctx = json_object_new_object();
        json_object *users = json_object_new_array();

        json_object *u1 = json_object_new_object();
        json_object *u1_books = json_object_new_array();
        json_object_array_add(u1_books, json_object_new_string("Book1"));
        json_object_object_add(u1, "books", u1_books);
        json_object_array_add(users, u1);

        json_object *u2 = json_object_new_object();
        json_object *u2_books = json_object_new_array();
        json_object_array_add(u2_books, json_object_new_string("Book2"));
        json_object_object_add(u2, "books", u2_books);
        json_object_array_add(users, u2);

        json_object_object_add(ctx, "users", users);

        char *result = render_template(env,
            "{{#each users}}{{#each books}}{{@../index}}-{{@index}}:{{this}} {{/each}}{{/each}}", ctx);
        ASSERT_STR_EQ("0-0:Book1 1-0:Book2 ", result, "nested each with @../index");
        free(result);
        json_object_put(ctx);
    }

    /* Subexpressions */
    {
        hbs_env_t *env2 = hbs_env_create();

        json_object *ctx = json_object_new_object();
        json_object *labels = json_object_new_array();
        json_object_array_add(labels, json_object_new_string("first"));
        json_object_array_add(labels, json_object_new_string("second"));
        json_object_array_add(labels, json_object_new_string("third"));
        json_object_object_add(ctx, "labels", labels);

        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("a"));
        json_object_array_add(items, json_object_new_string("b"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env2,
            "{{#each items}}{{lookup ../labels @index}} {{/each}}", ctx);
        ASSERT_STR_EQ("first second ", result, "lookup with @index in each");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env2);
    }

    /* #with block parameters */
    {
        json_object *ctx = json_object_new_object();
        json_object *city = json_object_new_object();
        json_object_object_add(city, "name", json_object_new_string("NYC"));
        json_object_object_add(ctx, "city", city);

        char *result = render_template(env,
            "{{#with city as |c|}}{{c.name}}{{/with}}", ctx);
        ASSERT_STR_EQ("NYC", result, "#with block parameters");
        free(result);
        json_object_put(ctx);
    }

    /* Standalone block: open/close on own lines */
    {
        char *result = render_template(env,
            "begin\n{{#if true}}\ncontent\n{{/if}}\nend",
            json_object_new_object());
        ASSERT_STR_EQ("begin\ncontent\nend", result,
            "standalone block strips open/close lines");
        free(result);
    }

    /* Standalone comment on own line */
    {
        char *result = render_template(env,
            "begin\n  {{! standalone comment }}\nend", NULL);
        ASSERT_STR_EQ("begin\nend", result,
            "standalone comment strips entire line");
        free(result);
    }

    /* Standalone block with else */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "show", json_object_new_boolean(0));

        char *result = render_template(env,
            "begin\n{{#if show}}\nyes\n{{else}}\nno\n{{/if}}\nend", ctx);
        ASSERT_STR_EQ("begin\nno\nend", result,
            "standalone block with else strips all tag lines");
        free(result);
        json_object_put(ctx);
    }

    /* Inline block (not standalone) should NOT strip */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "x", json_object_new_boolean(1));

        char *result = render_template(env, "A{{#if x}}B{{/if}}C", ctx);
        ASSERT_STR_EQ("ABC", result,
            "inline block does not strip");
        free(result);
        json_object_put(ctx);
    }

    /* Standalone partial on own line */
    {
        hbs_env_t *env2 = hbs_env_create();
        hbs_register_partial(env2, "p", "partial\n");

        char *result = render_template(env2, "begin\n  {{> p}}\nend", NULL);
        ASSERT_STR_EQ("begin\n  partial\nend", result,
            "standalone partial strips line and auto-indents");
        free(result);
        hbs_env_destroy(env2);
    }

    hbs_env_destroy(env);
}
