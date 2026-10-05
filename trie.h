#ifndef TRIE_H
#define TRIE_H
#define TRIE_MAX_WORD 64
#define TRIE_MAX_RESULTS 20

typedef struct TrieNode TrieNode;
struct TrieNode { TrieNode *child[26]; int word_index; };
typedef struct { TrieNode *root; char **words; int word_count; } Trie;

Trie *trie_create(void);
void trie_free(Trie *t);
int trie_insert(Trie *t,const char *word);
int trie_complete(const Trie *t,const char *prefix,const char **out,int max);

#endif
