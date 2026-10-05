#include "splay.h"
#include <stdlib.h>
#include <string.h>

static char *dupstr(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static SplayNode *node_new(const char *key) {
    SplayNode *n = calloc(1, sizeof(*n));
    if (!n) return NULL;
    n->key = dupstr(key);
    if (!n->key) { free(n); return NULL; }
    n->count = 1;
    return n;
}

static void rotate(SplayTree *t, SplayNode *x, int *rot) {
    SplayNode *p = x->parent;
    SplayNode *g = p->parent;
    if (x == p->left) {
        p->left = x->right;
        if (x->right) x->right->parent = p;
        x->right = p;
    } else {
        p->right = x->left;
        if (x->left) x->left->parent = p;
        x->left = p;
    }
    p->parent = x;
    x->parent = g;
    if (g) {
        if (g->left == p) g->left = x;
        else g->right = x;
    } else {
        t->root = x;
    }
    (*rot)++;
}

static void splay(SplayTree *t, SplayNode *x, int *rot) {
    while (x->parent) {
        SplayNode *p = x->parent;
        SplayNode *g = p->parent;
        if (!g) {
            rotate(t, x, rot);                 /* Zig */
        } else if ((g->left == p) == (p->left == x)) {
            rotate(t, p, rot);                 /* Zig-Zig */
            rotate(t, x, rot);
        } else {
            rotate(t, x, rot);                 /* Zig-Zag */
            rotate(t, x, rot);
        }
    }
}

void splay_init(SplayTree *tree) {
    memset(tree, 0, sizeof(*tree));
}

SplayNode *splay_find(SplayNode *root, const char *key) {
    while (root) {
        int c = strcmp(key, root->key);
        if (c == 0) return root;
        root = c < 0 ? root->left : root->right;
    }
    return NULL;
}

SplayNode *splay_access(SplayTree *t, const char *key, int *hit, int *depth, int *rotations) {
    *hit = 0;
    *depth = 0;
    *rotations = 0;

    SplayNode *n = t->root;
    SplayNode *parent = NULL;
    while (n) {
        parent = n;
        int c = strcmp(key, n->key);
        if (c == 0) {
            *hit = 1;
            break;
        }
        n = c < 0 ? n->left : n->right;
        (*depth)++;
    }

    if (*hit) {
        n->count++;
        t->hits++;
    } else {
        n = node_new(key);
        if (!n) return NULL;
        n->parent = parent;
        if (!parent) t->root = n;
        else if (strcmp(key, parent->key) < 0) parent->left = n;
        else parent->right = n;
    }

    t->searches++;
    splay(t, n, rotations);
    return n;
}

void splay_foreach(SplayNode *root, void (*fn)(SplayNode *, void *), void *ctx) {
    if (!root) return;
    splay_foreach(root->left, fn, ctx);
    fn(root, ctx);
    splay_foreach(root->right, fn, ctx);
}

void splay_reset_counts(SplayNode *n) {
    if (!n) return;
    n->count = 1;
    splay_reset_counts(n->left);
    splay_reset_counts(n->right);
}

void splay_free(SplayTree *t) {
    if (!t) return;

    /* The demo is small, but use a dynamically growing stack so cleanup
       remains safe if a larger workload is loaded later. */
    size_t cap = 64;
    size_t top = 0;
    SplayNode **stack = malloc(cap * sizeof(*stack));
    if (!stack) return;
    if (t->root) stack[top++] = t->root;

    while (top) {
        SplayNode *n = stack[--top];
        if (n->left) {
            if (top == cap) {
                cap *= 2;
                SplayNode **tmp = realloc(stack, cap * sizeof(*stack));
                if (!tmp) break;
                stack = tmp;
            }
            stack[top++] = n->left;
        }
        if (n->right) {
            if (top == cap) {
                cap *= 2;
                SplayNode **tmp = realloc(stack, cap * sizeof(*stack));
                if (!tmp) break;
                stack = tmp;
            }
            stack[top++] = n->right;
        }
        free(n->key);
        free(n);
    }
    free(stack);
    memset(t, 0, sizeof(*t));
}
