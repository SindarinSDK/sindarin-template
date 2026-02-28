# sindarin-template

A Handlebars-compatible template engine written in C11. Parses, compiles, and renders Handlebars templates using [json-c](https://github.com/json-c/json-c) for context data.

## Features

- Full Handlebars expression syntax (`{{expr}}`, `{{{unescaped}}}`, `{{&unescaped}}`)
- Built-in helpers: `if`, `unless`, `each`, `with`, `lookup`, `log`
- Chained `{{else if}}` / `{{else unless}}` / `{{else each}}` / `{{else with}}`
- Custom inline and block helpers with thread-safe `fn()` / `inverse()` callbacks
- Custom raw block helpers (`{{{{name}}}}...{{{{/name}}}}`)
- `helperMissing` / `blockHelperMissing` hooks
- `options.name`, `options.blockParams`, `hbs_create_frame()` for helper introspection
- Partials: static, dynamic, inline, nested, with context and hash parameters
- Partial blocks (`{{#> layout}}fallback{{/layout}}`) with `{{> @partial-block}}` yielding
- Standalone whitespace stripping for blocks, comments, and partials
- Partial auto-indentation
- Block parameters (`as |item index|`)
- Path expressions: dotted paths, `../` parent traversal, `@root`, `./` explicit context
- Private data variables: `@index`, `@key`, `@first`, `@last`
- Subexpressions: `{{> (lookup . "name")}}`
- Raw blocks: `{{{{raw}}}}...{{{{/raw}}}}`
- Strict mode, compat mode (recursive field lookup), noEscape option
- Whitespace control: `{{~expr~}}`
- HTML escaping of `& < > " ' = \``
- Escaped mustaches: `\{{literal}}`
- Segment literals: `{{[special-name]}}`
- Comments: `{{! short }}` and `{{!-- long --}}`

## Building

Requires CMake 3.14+ and a C11 compiler. Uses [vcpkg](https://vcpkg.io) for dependency management.

```bash
# Install dependencies
make deps

# Build library and tests
make build

# Run tests
make test

# Clean build artifacts
make clean
```

Set `VCPKG_ROOT` if your vcpkg installation is not at the default location:

```bash
VCPKG_ROOT=/path/to/vcpkg make build
```

## Quick Start

```c
#include <handlebars.h>

int main(void) {
    // Create environment
    hbs_env_t *env = hbs_env_create();

    // Compile template
    hbs_error_t err;
    hbs_template_t *tmpl = hbs_compile(env, "Hello, {{name}}!", &err);

    // Build context
    json_object *ctx = json_object_new_object();
    json_object_object_add(ctx, "name", json_object_new_string("World"));

    // Render
    char *result = hbs_render(tmpl, ctx, &err);
    printf("%s\n", result); // Hello, World!

    // Cleanup
    free(result);
    json_object_put(ctx);
    hbs_template_destroy(tmpl);
    hbs_env_destroy(env);
    return 0;
}
```

## API Reference

### Environment

```c
hbs_env_t *hbs_env_create(void);
void hbs_env_destroy(hbs_env_t *env);
```

Creates and destroys an environment that holds registered helpers and partials. All templates compiled with an environment share its helpers and partials.

#### Environment Options

```c
void hbs_env_set_no_escape(hbs_env_t *env, bool enabled);  // Disable HTML escaping globally
void hbs_env_set_compat(hbs_env_t *env, bool enabled);     // Enable recursive field lookup
void hbs_env_set_strict(hbs_env_t *env, bool enabled);     // Error on missing variables
```

- **noEscape**: When enabled, `{{expr}}` behaves like `{{{expr}}}` — no HTML escaping.
- **compat**: When enabled, path resolution walks up parent contexts when a field is not found on the current context (Handlebars.js compatibility mode).
- **strict**: When enabled, referencing a missing variable in `{{expr}}` causes `hbs_render()` to return `NULL` with `HBS_ERR_RENDER`. Use `hbs_render_error_message()` to get the detail message.

### Template Compilation

```c
hbs_template_t *hbs_compile(hbs_env_t *env, const char *source, hbs_error_t *err);
void hbs_template_destroy(hbs_template_t *tmpl);
```

Compiles a Handlebars template string into an internal representation. The compiled template holds a reference to `env` (does not copy it). Returns `NULL` on error with `err` set.

### Rendering

```c
char *hbs_render(hbs_template_t *tmpl, json_object *context, hbs_error_t *err);
```

Renders a compiled template with the given JSON context. Returns a `malloc`-allocated string that the caller must `free()`. Returns `NULL` on error.

```c
const char *hbs_render_error_message(hbs_template_t *tmpl);
```

Returns the error detail message from the last failed render (e.g., strict mode violations). Returns `NULL` if the last render succeeded.

### Helpers

```c
typedef char *(*hbs_helper_fn)(json_object **params, int param_count, hbs_options_t *options);

hbs_error_t hbs_register_helper(hbs_env_t *env, const char *name, hbs_helper_fn fn);
hbs_error_t hbs_unregister_helper(hbs_env_t *env, const char *name);
```

Register or unregister custom helpers. Helper functions receive resolved parameters and an options struct:

```c
typedef struct hbs_options {
    json_object *hash;                              // Hash arguments (key=value pairs)
    json_object *data;                              // Private data (@index, @key, etc.)
    json_object *context;                           // Current context object
    json_object **params;                           // Resolved positional parameters
    int param_count;
    const char *name;                               // Name of the helper being invoked
    char **block_params;                            // Block parameter names (as |x y|)
    int block_param_count;
    char *(*fn)(json_object *ctx, void *data);      // Render block body with context
    char *(*inverse)(json_object *ctx, void *data); // Render else block with context
    void *fn_data;                                  // Closure data for fn (pass to fn)
    void *inverse_data;                             // Closure data for inverse (pass to inverse)
    void *_internal;                                // Internal (do not use)
} hbs_options_t;
```

Helpers return a `malloc`-allocated string (or `NULL`). The returned string is inserted into the output as-is (SafeString semantics -- no HTML escaping).

#### Inline helper example

```c
static char *helper_bold(json_object **params, int param_count, hbs_options_t *options) {
    if (param_count > 0 && params[0]) {
        const char *text = json_object_get_string(params[0]);
        char *result = malloc(strlen(text) + 8);
        sprintf(result, "<b>%s</b>", text);
        return result;
    }
    return strdup("");
}

hbs_register_helper(env, "bold", helper_bold);
// {{bold name}} -> <b>World</b>
```

#### Block helper example

```c
static char *helper_noop(json_object **params, int param_count, hbs_options_t *options) {
    if (options->fn) {
        return options->fn(options->context, options->fn_data);
    }
    return strdup("");
}

hbs_register_helper(env, "noop", helper_noop);
// {{#noop}}Hello {{name}}{{/noop}} -> Hello World
```

#### helperMissing / blockHelperMissing

Register a helper named `"helperMissing"` to handle calls to undefined helpers that have parameters or hash arguments. Register `"blockHelperMissing"` for undefined block helpers.

```c
static char *my_helper_missing(json_object **params, int param_count, hbs_options_t *options) {
    return strdup("[missing helper]");
}

hbs_register_helper(env, "helperMissing", my_helper_missing);
// {{unknownHelper arg}} -> [missing helper]
// {{unknownVar}}        -> (empty string, helperMissing not invoked for simple paths)
```

### Partials

```c
hbs_error_t hbs_register_partial(hbs_env_t *env, const char *name, const char *source);
hbs_error_t hbs_unregister_partial(hbs_env_t *env, const char *name);
```

Register or unregister named partial templates.

```c
hbs_register_partial(env, "header", "<h1>{{title}}</h1>");
// {{> header}} -> <h1>My Title</h1>
```

### Utilities

```c
json_object *hbs_create_frame(hbs_options_t *options);  // Create private data frame (caller must json_object_put())
char *hbs_escape_html(const char *input);               // Caller must free()
const char *hbs_error_string(hbs_error_t err);
```

`hbs_create_frame()` creates a shallow copy of `options->data` for use in custom helpers that need to set private data variables (like `@index`, `@key`).

### Error Codes

| Code | Meaning |
|------|---------|
| `HBS_OK` | Success |
| `HBS_ERR_PARSE` | Template parse error |
| `HBS_ERR_RENDER` | Render error |
| `HBS_ERR_MEMORY` | Memory allocation failure |
| `HBS_ERR_NOT_FOUND` | Helper or partial not found |
| `HBS_ERR_INVALID_ARG` | Invalid argument |

## Template Syntax

### Expressions

```handlebars
{{variable}}             Output variable (HTML-escaped)
{{{variable}}}           Output variable (unescaped)
{{&variable}}            Output variable (unescaped, alternative syntax)
{{person.name}}          Dotted path
{{this}}                 Current context
{{./explicit}}           Explicit context path (skip helper lookup)
{{@root.title}}          Root context access
{{../name}}              Parent context access
{{[special-name]}}       Segment literal for special characters
\{{escaped}}             Literal {{ in output
```

### Built-in Helpers

```handlebars
{{#if condition}}...{{else}}...{{/if}}
{{#if val includeZero=true}}...{{/if}}
{{#unless condition}}...{{/unless}}

{{#each items}}
  {{@index}} {{@first}} {{@last}} {{this}}
{{else}}
  No items.
{{/each}}

{{#each object}}
  {{@key}}: {{this}}
{{/each}}

{{#with person}}{{name}}{{else}}No person.{{/with}}

{{lookup array index}}
{{lookup object key}}

{{log "message" level="warn"}}
```

### Block Parameters

```handlebars
{{#each users as |user idx|}}
  {{idx}}: {{user.name}}
{{/each}}

{{#with city as |c|}}
  {{c.name}}
{{/with}}
```

### Chained Else

```handlebars
{{#if a}}
  A
{{else if b}}
  B
{{else}}
  Neither
{{/if}}
```

### Partials

```handlebars
{{> partialName}}                   Basic partial
{{> partialName context}}           Partial with context
{{> partialName key="value"}}       Partial with hash parameters
{{> (lookup . "name")}}             Dynamic partial

{{#> layout}}                       Partial block (fallback if missing)
  Fallback content
{{/layout}}

{{> @partial-block}}                Yield to caller's block (inside a partial)

{{#*inline "myPartial"}}            Inline partial definition
  Content here
{{/inline}}
```

#### Partial Blocks and Yielding

A partial can yield back to its caller using `{{> @partial-block}}`:

```c
hbs_register_partial(env, "layout", "<header/>{{> @partial-block}}<footer/>");
```

```handlebars
{{#> layout}}
  Page content here
{{/layout}}
```

Output: `<header/>Page content here<footer/>`

If the partial does not exist, the block body renders as a fallback.

#### Auto-Indentation

When a partial tag appears on a standalone line (only whitespace before it), every line of the partial's output is indented by that whitespace:

```handlebars
<ul>
  {{> listItems}}
</ul>
```

If `listItems` is `<li>A</li>\n<li>B</li>\n`, the output is:

```html
<ul>
  <li>A</li>
  <li>B</li>
</ul>
```

### Raw Blocks

```handlebars
{{{{raw}}}}
  {{not-processed}}
{{{{/raw}}}}
```

Output: `  {{not-processed}}`

### Comments

```handlebars
{{! short comment }}
{{!-- long comment with {{ braces }} --}}
```

Comments produce no output. Standalone comments on their own line have the entire line stripped.

### Whitespace Control

The `~` character strips whitespace on the corresponding side:

```handlebars
{{~expr}}     Strip whitespace before
{{expr~}}     Strip whitespace after
{{~expr~}}    Strip both sides
```

### Standalone Whitespace Stripping

Block tags, comments, partials, and inline partials that appear on a line by themselves (with only whitespace) have their entire line removed from the output:

```handlebars
begin
{{#if show}}
content
{{/if}}
end
```

Output (when `show` is true): `begin\ncontent\nend` -- the `{{#if}}` and `{{/if}}` lines are stripped.

This does not apply to tags that share a line with other content.

## Project Structure

```
sindarin-template/
  include/
    handlebars.h        Public API header
  src/
    lexer.h/c           Tokenization
    parser.h/c          AST generation
    ast.h/c             AST node definitions
    render.h/c          Rendering engine, environment, public API
    utils.h/c           String buffers, HTML escaping
  test/
    test.h              Test macros
    test_main.c         Test runner
    test_expressions.c  Expression and path tests
    test_helpers.c      Built-in and custom helper tests
    test_escaping.c     HTML escaping and whitespace control tests
    test_partials.c     Partial rendering tests
    test_blocks.c       Block helpers, standalone stripping tests
  CMakeLists.txt        Build configuration
  Makefile              Build shortcuts
  vcpkg.json            Dependency manifest
```

## Dependencies

- [json-c](https://github.com/json-c/json-c) -- JSON object model for template context data

## License

MIT
