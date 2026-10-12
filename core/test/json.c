#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct parser {
    json_doc *doc;
    const char *p, *end;
    const char *err;
} parser;

static void skip_ws(parser *ps)
{
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\n' || *ps->p == '\r' || *ps->p == '\t'))
        ps->p++;
}

static int new_node(parser *ps, int type)
{
    json_doc *d = ps->doc;
    json_node *n;

    if (d->used == d->cap) {
        int cap = d->cap ? 2 * d->cap : 1024;
        json_node *grown = realloc(d->nodes, (size_t)cap * sizeof *grown);
        if (!grown) {
            ps->err = "out of memory";
            return -1;
        }
        d->nodes = grown;
        d->cap = cap;
    }
    n = &d->nodes[d->used];
    n->type = type;
    n->text = ps->p;
    n->len = 0;
    n->key = -1;
    n->first = n->next = -1;
    n->count = 0;
    return d->used++;
}

static int parse_value(parser *ps);

static int parse_string(parser *ps)
{
    int id;

    ps->p++;                                /* opening quote */
    id = new_node(ps, JSON_STR);
    if (id < 0)
        return -1;
    while (ps->p < ps->end && *ps->p != '"') {
        if (*ps->p == '\\')
            ps->p++;
        ps->p++;
    }
    if (ps->p >= ps->end) {
        ps->err = "unterminated string";
        return -1;
    }
    ps->doc->nodes[id].len = (size_t)(ps->p - ps->doc->nodes[id].text);
    ps->p++;
    return id;
}

/* Appends child to parent; `last` tracks the last child for O(1) appends. */
static void append(json_doc *d, int parent, int *last, int child)
{
    if (*last < 0)
        d->nodes[parent].first = child;
    else
        d->nodes[*last].next = child;
    *last = child;
    d->nodes[parent].count++;
}

static int parse_container(parser *ps, int type, char close)
{
    int id = new_node(ps, type), last = -1;

    if (id < 0)
        return -1;
    ps->p++;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == close) {
        ps->p++;
        return id;
    }
    for (;;) {
        int key = -1, val;

        skip_ws(ps);
        if (type == JSON_OBJ) {
            if (ps->p >= ps->end || *ps->p != '"') {
                ps->err = "expected key";
                return -1;
            }
            key = parse_string(ps);
            if (key < 0)
                return -1;
            skip_ws(ps);
            if (ps->p >= ps->end || *ps->p != ':') {
                ps->err = "expected ':'";
                return -1;
            }
            ps->p++;
        }
        val = parse_value(ps);
        if (val < 0)
            return -1;
        ps->doc->nodes[val].key = key;
        append(ps->doc, id, &last, val);
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',') {
            ps->p++;
            continue;
        }
        if (ps->p < ps->end && *ps->p == close) {
            ps->p++;
            return id;
        }
        ps->err = "expected ',' or end of container";
        return -1;
    }
}

static int parse_word(parser *ps, const char *w, int type)
{
    size_t n = strlen(w);

    if ((size_t)(ps->end - ps->p) < n || memcmp(ps->p, w, n) != 0) {
        ps->err = "unknown literal";
        return -1;
    }
    {
        int id = new_node(ps, type);
        ps->p += n;
        return id;
    }
}

static int parse_value(parser *ps)
{
    skip_ws(ps);
    if (ps->p >= ps->end) {
        ps->err = "unexpected end";
        return -1;
    }
    switch (*ps->p) {
    case '{': return parse_container(ps, JSON_OBJ, '}');
    case '[': return parse_container(ps, JSON_ARR, ']');
    case '"': return parse_string(ps);
    case 't': return parse_word(ps, "true", JSON_BOOL);
    case 'f': return parse_word(ps, "false", JSON_BOOL);
    case 'n': return parse_word(ps, "null", JSON_NULL);
    case 'N': return parse_word(ps, "NaN", JSON_NUM);         /* Python's json writes these */
    case 'I': return parse_word(ps, "Infinity", JSON_NUM);
    default: {
        int id = new_node(ps, JSON_NUM);
        if (id < 0)
            return -1;
        if (*ps->p == '-')
            ps->p++;
        if (ps->p < ps->end && *ps->p == 'I')
            ps->p += 8;                                         /* -Infinity */
        while (ps->p < ps->end && strchr("0123456789.eE+-", *ps->p))
            ps->p++;
        if (ps->p == ps->doc->nodes[id].text) {
            ps->err = "bad number";
            return -1;
        }
        return id;
    }
    }
}

int json_load(json_doc *doc, const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    parser ps;

    memset(doc, 0, sizeof *doc);
    if (!f) {
        fprintf(stderr, "%s: cannot open\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    doc->src = malloc((size_t)size + 1);
    if (!doc->src || fread(doc->src, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        fprintf(stderr, "%s: cannot read\n", path);
        return -1;
    }
    fclose(f);
    doc->src[size] = 0;
    ps.doc = doc;
    ps.p = doc->src;
    ps.end = doc->src + size;
    ps.err = NULL;
    if (parse_value(&ps) != 0 || ps.err) {
        fprintf(stderr, "%s: %s at byte %ld\n", path, ps.err ? ps.err : "parse error",
                (long)(ps.p - doc->src));
        json_free(doc);
        return -1;
    }
    return 0;
}

void json_free(json_doc *doc)
{
    free(doc->src);
    free(doc->nodes);
    memset(doc, 0, sizeof *doc);
}

int json_get(const json_doc *doc, int obj, const char *key)
{
    int c;
    size_t n = strlen(key);

    if (obj < 0 || doc->nodes[obj].type != JSON_OBJ)
        return -1;
    for (c = doc->nodes[obj].first; c >= 0; c = doc->nodes[c].next) {
        const json_node *k = &doc->nodes[doc->nodes[c].key];
        if (k->len == n && memcmp(k->text, key, n) == 0)
            return c;
    }
    return -1;
}

int json_at(const json_doc *doc, int arr, int index)
{
    int c, i = 0;

    if (arr < 0 || doc->nodes[arr].type != JSON_ARR || index < 0)
        return -1;
    for (c = doc->nodes[arr].first; c >= 0; c = doc->nodes[c].next, i++)
        if (i == index)
            return c;
    return -1;
}

int json_path(const json_doc *doc, int node, const char *path)
{
    char part[128];

    while (node >= 0 && *path) {
        size_t n = strcspn(path, "/");
        if (n >= sizeof part)
            return -1;
        memcpy(part, path, n);
        part[n] = 0;
        if (doc->nodes[node].type == JSON_ARR)
            node = json_at(doc, node, atoi(part));
        else
            node = json_get(doc, node, part);
        path += n;
        if (*path == '/')
            path++;
    }
    return node;
}

double json_num(const json_doc *doc, int node)
{
    if (node < 0 || doc->nodes[node].type != JSON_NUM)
        return 0.0;
    return strtod(doc->nodes[node].text, NULL);
}

int json_str_eq(const json_doc *doc, int node, const char *s)
{
    size_t n = strlen(s);

    return node >= 0 && doc->nodes[node].type == JSON_STR &&
           doc->nodes[node].len == n && memcmp(doc->nodes[node].text, s, n) == 0;
}
