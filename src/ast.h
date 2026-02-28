#ifndef HBS_AST_H
#define HBS_AST_H

#include <stdbool.h>
#include <stddef.h>

/* AST node types */
typedef enum {
    HBS_AST_PROGRAM,        /* Root node: list of statements */
    HBS_AST_TEXT,            /* Literal text */
    HBS_AST_MUSTACHE,       /* {{ expression }} */
    HBS_AST_BLOCK,          /* {{#helper}}...{{/helper}} */
    HBS_AST_PARTIAL,        /* {{> partial }} */
    HBS_AST_COMMENT,        /* {{! comment }} */
    HBS_AST_RAW_BLOCK,      /* {{{{raw}}}}...{{{{/raw}}}} */
    HBS_AST_SUBEXPR,        /* (helper arg1 arg2) nested expression */
    HBS_AST_INLINE_PARTIAL, /* {{#*inline "name"}}...{{/inline}} */
    HBS_AST_LITERAL,        /* Literal value: string, number, boolean, null, undefined */
} hbs_ast_type_t;

/* Literal types */
typedef enum {
    HBS_LIT_STRING,
    HBS_LIT_NUMBER,
    HBS_LIT_BOOLEAN,
    HBS_LIT_NULL,
    HBS_LIT_UNDEFINED,
} hbs_literal_type_t;

/* A path expression like "person.name" or "../title" */
typedef struct {
    char **parts;           /* Array of path segments */
    int part_count;
    int depth;              /* Number of ../ traversals */
    bool is_this;           /* Refers to current context (this or .) */
    bool is_context_explicit; /* Starts with ./ — skip helper lookup */
} hbs_path_t;

/* A hash pair: key=value */
typedef struct {
    char *key;
    struct hbs_ast_node *value;
} hbs_hash_pair_t;

/* Forward declaration */
typedef struct hbs_ast_node hbs_ast_node_t;

/* AST node */
struct hbs_ast_node {
    hbs_ast_type_t type;

    union {
        /* HBS_AST_PROGRAM */
        struct {
            hbs_ast_node_t **statements;
            int statement_count;
        } program;

        /* HBS_AST_TEXT */
        struct {
            char *value;
        } text;

        /* HBS_AST_MUSTACHE */
        struct {
            hbs_path_t *path;               /* The expression path or helper name */
            hbs_ast_node_t **params;         /* Positional parameters */
            int param_count;
            hbs_hash_pair_t *hash_pairs;     /* key=value pairs */
            int hash_count;
            bool escaped;                    /* true for {{expr}}, false for {{{expr}}} */
            bool strip_left;                /* ~}} whitespace control */
            bool strip_right;               /* {{~ whitespace control */
        } mustache;

        /* HBS_AST_BLOCK */
        struct {
            hbs_path_t *path;               /* Helper name (if, each, with, custom) */
            hbs_ast_node_t **params;
            int param_count;
            hbs_hash_pair_t *hash_pairs;
            int hash_count;
            hbs_ast_node_t *body;           /* Main block (program node) */
            hbs_ast_node_t *inverse;        /* {{else}} block (program node or NULL) */
            char **block_params;            /* as |x y| block parameter names */
            int block_param_count;
            bool strip_open_left;
            bool strip_open_right;
            bool strip_close_left;
            bool strip_close_right;
            bool is_partial_block;          /* {{#> partial}} */
        } block;

        /* HBS_AST_PARTIAL */
        struct {
            hbs_path_t *name;               /* Partial name (NULL if dynamic) */
            hbs_ast_node_t *dynamic_name;   /* Subexpression for dynamic partial */
            hbs_ast_node_t *context;        /* Optional context expression */
            hbs_hash_pair_t *hash_pairs;
            int hash_count;
            char *indent;                   /* Standalone indentation prefix */
        } partial;

        /* HBS_AST_COMMENT */
        struct {
            char *value;
        } comment;

        /* HBS_AST_RAW_BLOCK */
        struct {
            char *helper_name;
            char *content;                  /* Raw, unprocessed content */
        } raw_block;

        /* HBS_AST_SUBEXPR */
        struct {
            hbs_path_t *path;               /* Helper name */
            hbs_ast_node_t **params;
            int param_count;
            hbs_hash_pair_t *hash_pairs;
            int hash_count;
        } subexpr;

        /* HBS_AST_INLINE_PARTIAL */
        struct {
            char *name;                     /* Partial name */
            hbs_ast_node_t *body;           /* Body program */
        } inline_partial;

        /* HBS_AST_LITERAL */
        struct {
            hbs_literal_type_t lit_type;
            char *value;                    /* String representation */
        } literal;
    };
};

/* Creation */
hbs_ast_node_t *hbs_ast_program(void);
hbs_ast_node_t *hbs_ast_text(const char *value);
hbs_ast_node_t *hbs_ast_mustache(hbs_path_t *path, bool escaped);
hbs_ast_node_t *hbs_ast_block(hbs_path_t *path);
hbs_ast_node_t *hbs_ast_comment(const char *value);
hbs_ast_node_t *hbs_ast_partial(hbs_path_t *name);
hbs_ast_node_t *hbs_ast_raw_block(const char *helper_name, const char *content);
hbs_ast_node_t *hbs_ast_subexpr(hbs_path_t *path);
hbs_ast_node_t *hbs_ast_inline_partial(const char *name);
hbs_ast_node_t *hbs_ast_literal(hbs_literal_type_t lit_type, const char *value);

/* Add a statement to a program node */
void hbs_ast_program_add(hbs_ast_node_t *program, hbs_ast_node_t *stmt);

/* Path creation */
hbs_path_t *hbs_path_create(void);
void hbs_path_add_part(hbs_path_t *path, const char *part);
void hbs_path_destroy(hbs_path_t *path);

/* Destruction */
void hbs_ast_destroy(hbs_ast_node_t *node);

#endif /* HBS_AST_H */
