/* A small JSON reader for the host tests. Reads the whole text into a tree
 * of nodes; strings are not unescaped (the vector files use plain ASCII
 * keys). Test code only, not part of the core. */
#ifndef JSON_H
#define JSON_H

#include <stddef.h>

enum { JSON_NULL, JSON_BOOL, JSON_NUM, JSON_STR, JSON_ARR, JSON_OBJ };

typedef struct json_node {
    int type;
    const char *text;   /* start in the source; for JSON_STR after the quote */
    size_t len;         /* length of a string's content */
    int key;            /* index of the key node for object members, else -1 */
    int first, next;    /* first child, next sibling; -1 if none */
    int count;          /* number of children */
} json_node;

typedef struct json_doc {
    char *src;
    json_node *nodes;
    int used, cap;
} json_doc;

/* Reads a file; returns 0 on success, prints the reason and returns -1 otherwise. */
int json_load(json_doc *doc, const char *path);
void json_free(json_doc *doc);

/* Node 0 is the root. All lookups return -1 if not found or on -1 input. */
int json_get(const json_doc *doc, int obj, const char *key);
int json_at(const json_doc *doc, int arr, int index);
/* Follows a path like "runs/1/checks/start/t"; numbers index arrays. */
int json_path(const json_doc *doc, int node, const char *path);
double json_num(const json_doc *doc, int node);
int json_str_eq(const json_doc *doc, int node, const char *s);

#endif
