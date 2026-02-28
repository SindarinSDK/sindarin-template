#include "handlebars.h"
#include "test.h"

void test_expressions(void) {
    TEST_SUITE("Basic Expressions");

    hbs_env_t *env = hbs_env_create();

    /* Simple variable */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("World"));

        char *result = render_template(env, "Hello, {{name}}!", ctx);
        ASSERT_STR_EQ("Hello, World!", result, "simple variable substitution");
        free(result);
        json_object_put(ctx);
    }

    /* Plain text without expressions */
    {
        char *result = render_template(env, "Hello, World!", NULL);
        ASSERT_STR_EQ("Hello, World!", result, "plain text passthrough");
        free(result);
    }

    /* Nested property with dot notation */
    {
        json_object *ctx = json_object_new_object();
        json_object *person = json_object_new_object();
        json_object_object_add(person, "firstname", json_object_new_string("Alice"));
        json_object_object_add(person, "lastname", json_object_new_string("Smith"));
        json_object_object_add(ctx, "person", person);

        char *result = render_template(env, "{{person.firstname}} {{person.lastname}}", ctx);
        ASSERT_STR_EQ("Alice Smith", result, "dot notation nested properties");
        free(result);
        json_object_put(ctx);
    }

    /* Missing variable produces empty string */
    {
        json_object *ctx = json_object_new_object();
        char *result = render_template(env, "Hello, {{name}}!", ctx);
        ASSERT_STR_EQ("Hello, !", result, "missing variable produces empty");
        free(result);
        json_object_put(ctx);
    }

    /* Multiple expressions */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "first", json_object_new_string("John"));
        json_object_object_add(ctx, "last", json_object_new_string("Doe"));

        char *result = render_template(env, "{{first}} {{last}}", ctx);
        ASSERT_STR_EQ("John Doe", result, "multiple expressions");
        free(result);
        json_object_put(ctx);
    }

    /* Integer and boolean values */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "count", json_object_new_int(42));
        json_object_object_add(ctx, "active", json_object_new_boolean(1));

        char *result = render_template(env, "Count: {{count}}, Active: {{active}}", ctx);
        ASSERT_STR_EQ("Count: 42, Active: true", result, "integer and boolean values");
        free(result);
        json_object_put(ctx);
    }

    /* Deeply nested property */
    {
        json_object *ctx = json_object_new_object();
        json_object *a = json_object_new_object();
        json_object *b = json_object_new_object();
        json_object_object_add(b, "c", json_object_new_string("deep"));
        json_object_object_add(a, "b", b);
        json_object_object_add(ctx, "a", a);

        char *result = render_template(env, "{{a.b.c}}", ctx);
        ASSERT_STR_EQ("deep", result, "deeply nested property (a.b.c)");
        free(result);
        json_object_put(ctx);
    }

    /* @root access */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "title", json_object_new_string("Main"));
        json_object *person = json_object_new_object();
        json_object_object_add(person, "name", json_object_new_string("Alice"));
        json_object_object_add(ctx, "person", person);

        char *result = render_template(env, "{{#with person}}{{name}} - {{@root.title}}{{/with}}", ctx);
        ASSERT_STR_EQ("Alice - Main", result, "@root access from nested context");
        free(result);
        json_object_put(ctx);
    }

    /* ../parent access in each */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "prefix", json_object_new_string("item"));
        json_object *items = json_object_new_array();
        json_object_array_add(items, json_object_new_string("a"));
        json_object_array_add(items, json_object_new_string("b"));
        json_object_object_add(ctx, "items", items);

        char *result = render_template(env, "{{#each items}}{{../prefix}}-{{this}} {{/each}}", ctx);
        ASSERT_STR_EQ("item-a item-b ", result, "../ parent context access in each");
        free(result);
        json_object_put(ctx);
    }

    /* Escaped mustaches \{{ */
    {
        char *result = render_template(env, "\\{{not-expression}}", NULL);
        ASSERT_STR_EQ("{{not-expression}}", result, "escaped mustaches with backslash");
        free(result);
    }

    /* Segment literal notation */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "foo-bar", json_object_new_string("works"));

        char *result = render_template(env, "{{[foo-bar]}}", ctx);
        ASSERT_STR_EQ("works", result, "segment literal [foo-bar]");
        free(result);
        json_object_put(ctx);
    }

    /* ./explicit context path (skip helper lookup) */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("context-value"));

        char *result = render_template(env, "{{./name}}", ctx);
        ASSERT_STR_EQ("context-value", result, "./ explicit context path");
        free(result);
        json_object_put(ctx);
    }

    hbs_env_destroy(env);
}
