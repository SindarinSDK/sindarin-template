#include "handlebars.h"
#include "test.h"

void test_escaping(void) {
    TEST_SUITE("HTML Escaping & Whitespace Control");

    hbs_env_t *env = hbs_env_create();

    /* Double-stash escapes HTML */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "html", json_object_new_string("<b>bold</b>"));

        char *result = render_template(env, "{{html}}", ctx);
        ASSERT_STR_EQ("&lt;b&gt;bold&lt;/b&gt;", result, "{{}} escapes HTML");
        free(result);
        json_object_put(ctx);
    }

    /* Triple-stash does NOT escape HTML */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "html", json_object_new_string("<b>bold</b>"));

        char *result = render_template(env, "{{{html}}}", ctx);
        ASSERT_STR_EQ("<b>bold</b>", result, "{{{}}} does not escape HTML");
        free(result);
        json_object_put(ctx);
    }

    /* Escapes ampersands */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "val", json_object_new_string("a & b"));

        char *result = render_template(env, "{{val}}", ctx);
        ASSERT_STR_EQ("a &amp; b", result, "escapes ampersands");
        free(result);
        json_object_put(ctx);
    }

    /* Escapes quotes */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "val", json_object_new_string("say \"hello\""));

        char *result = render_template(env, "{{val}}", ctx);
        ASSERT_STR_EQ("say &quot;hello&quot;", result, "escapes double quotes");
        free(result);
        json_object_put(ctx);
    }

    /* Escapes single quotes */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "val", json_object_new_string("it's"));

        char *result = render_template(env, "{{val}}", ctx);
        ASSERT_STR_EQ("it&#x27;s", result, "escapes single quotes");
        free(result);
        json_object_put(ctx);
    }

    /* Escapes backtick */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "val", json_object_new_string("code `here`"));

        char *result = render_template(env, "{{val}}", ctx);
        ASSERT_STR_EQ("code &#x60;here&#x60;", result, "escapes backticks");
        free(result);
        json_object_put(ctx);
    }

    /* Escapes equals sign */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "val", json_object_new_string("a=b"));

        char *result = render_template(env, "{{val}}", ctx);
        ASSERT_STR_EQ("a&#x3D;b", result, "escapes equals sign");
        free(result);
        json_object_put(ctx);
    }

    /* {{&expr}} unescaped syntax */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "html", json_object_new_string("<b>bold</b>"));

        char *result = render_template(env, "{{&html}}", ctx);
        ASSERT_STR_EQ("<b>bold</b>", result, "{{&}} does not escape HTML");
        free(result);
        json_object_put(ctx);
    }

    /* {{&expr}} with params */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "val", json_object_new_string("a & \"b\""));

        char *result = render_template(env, "{{&val}}", ctx);
        ASSERT_STR_EQ("a & \"b\"", result, "{{&}} preserves special chars");
        free(result);
        json_object_put(ctx);
    }

    /* Whitespace control: {{~ strips left */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("World"));

        char *result = render_template(env, "Hello   {{~name}}", ctx);
        ASSERT_STR_EQ("HelloWorld", result, "{{~ strips left whitespace");
        free(result);
        json_object_put(ctx);
    }

    /* Whitespace control: ~}} strips right */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("Hello"));

        char *result = render_template(env, "{{name~}}   World", ctx);
        ASSERT_STR_EQ("HelloWorld", result, "~}} strips right whitespace");
        free(result);
        json_object_put(ctx);
    }

    /* Whitespace control: both sides */
    {
        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "name", json_object_new_string("X"));

        char *result = render_template(env, "A   {{~name~}}   B", ctx);
        ASSERT_STR_EQ("AXB", result, "{{~ ~}} strips both sides");
        free(result);
        json_object_put(ctx);
    }

    hbs_env_destroy(env);

    /* noEscape: {{expr}} does not escape when noEscape is set */
    {
        hbs_env_t *env2 = hbs_env_create();
        hbs_env_set_no_escape(env2, true);

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "html", json_object_new_string("<b>bold</b>"));

        char *result = render_template(env2, "{{html}}", ctx);
        ASSERT_STR_EQ("<b>bold</b>", result, "noEscape: {{}} does not escape HTML");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env2);
    }

    /* noEscape off: {{expr}} still escapes (default) */
    {
        hbs_env_t *env2 = hbs_env_create();

        json_object *ctx = json_object_new_object();
        json_object_object_add(ctx, "html", json_object_new_string("<b>bold</b>"));

        char *result = render_template(env2, "{{html}}", ctx);
        ASSERT_STR_EQ("&lt;b&gt;bold&lt;/b&gt;", result, "noEscape off: {{}} still escapes");
        free(result);
        json_object_put(ctx);
        hbs_env_destroy(env2);
    }
}
