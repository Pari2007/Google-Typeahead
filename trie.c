#include "trie.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static TrieNode *new_node(void) {
    TrieNode *n = calloc(1, sizeof(*n));
    if (n) n->word_index = -1;
    return n;
}

static char *dupstr2(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

Trie *trie_create(void) {
    Trie *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->root = new_node();
    if (!t->root) { free(t); return NULL; }
    return t;
}

int trie_insert(Trie *t, const char *word) {
    if (!t || !word || !*word) return 0;

    TrieNode *n = t->root;
    for (const unsigned char *p = (const unsigned char *)word; *p; p++) {
        if (!isalpha(*p)) return 0;
        int i = tolower(*p) - 'a';
        if (i < 0 || i > 25) return 0;
        if (!n->child[i]) {
            n->child[i] = new_node();
            if (!n->child[i]) return 0;
        }
        n = n->child[i];
    }

    if (n->word_index < 0) {
        char **nw = realloc(t->words, (size_t)(t->word_count + 1) * sizeof(*nw));
        if (!nw) return 0;
        t->words = nw;
        t->words[t->word_count] = dupstr2(word);
        if (!t->words[t->word_count]) return 0;
        n->word_index = t->word_count++;
    }
    return 1;
}

static void collect(const Trie *t, TrieNode *n, const char **out, int *count, int max) {
    if (!n || *count >= max) return;
    if (n->word_index >= 0) out[(*count)++] = t->words[n->word_index];
    for (int i = 0; i < 26 && *count < max; i++) {
        collect(t, n->child[i], out, count, max);
    }
}

int trie_complete(const Trie *t, const char *prefix, const char **out, int max) {
    if (!t || !prefix || !out || max <= 0) return 0;
    TrieNode *n = t->root;
    for (const unsigned char *p = (const unsigned char *)prefix; *p; p++) {
        if (!isalpha(*p)) return 0;
        int i = tolower(*p) - 'a';
        if (i < 0 || i > 25 || !n->child[i]) return 0;
        n = n->child[i];
    }
    int c = 0;
    collect(t, n, out, &c, max);
    return c;
}

static void free_node(TrieNode *n) {
    if (!n) return;
    for (int i = 0; i < 26; i++) free_node(n->child[i]);
    free(n);
}

void trie_free(Trie *t) {
    if (!t) return;
    free_node(t->root);
    for (int i = 0; i < t->word_count; i++) free(t->words[i]);
    free(t->words);
    free(t);
}
