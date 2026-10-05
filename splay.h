#ifndef SPLAY_H
#define SPLAY_H

typedef struct SplayNode {
    char *key;
    int count;
    struct SplayNode *left, *right, *parent;
} SplayNode;

typedef struct {
    SplayNode *root;
    unsigned long searches;
    unsigned long hits;
} SplayTree;

void splay_init(SplayTree *tree);
void splay_free(SplayTree *tree);
SplayNode *splay_access(SplayTree *tree, const char *key, int *hit, int *depth, int *rotations);
void splay_reset_counts(SplayNode *node);
void splay_foreach(SplayNode *root, void (*fn)(SplayNode *, void *), void *ctx);
SplayNode *splay_find(SplayNode *root, const char *key);

#endif
