#define _POSIX_C_SOURCE 200809L
#include "splay.h"
#include "trie.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <sys/time.h>
#define REQUEST_CAP 65536
#define JSON_CAP 16384

static SplayTree tree;
static Trie *trie;
static volatile sig_atomic_t running = 1;

static const char *SEEDS[] = {
    "decision tree", "interval tree", "splay tree",
    "markov model", "bayesian network", "knowledge graph", "neural network"
};
static const size_t SEED_COUNT = sizeof(SEEDS) / sizeof(SEEDS[0]);

static void stop_server(int sig) { (void)sig; running = 0; }

static void send_response(int fd, const char *status, const char *type, const char *body) {
    char header[512];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Expires: 0\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        status, type, strlen(body));
    if (n > 0) send(fd, header, (size_t)n, 0);
    send(fd, body, strlen(body), 0);
}

static void send_json(int fd, const char *status, const char *body) {
    send_response(fd, status, "application/json", body);
}

static void send_file(int fd, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        send_json(fd, "404 Not Found", "{\"error\":\"file not found\"}");
        return;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); send_json(fd, "500 Internal Server Error", "{\"error\":\"file error\"}"); return; }
    long sz = ftell(f);
    if (sz < 0 || sz > 1024 * 1024) { fclose(f); send_json(fd, "500 Internal Server Error", "{\"error\":\"invalid file size\"}"); return; }
    rewind(f);

    char *body = malloc((size_t)sz + 1);
    if (!body) { fclose(f); send_json(fd, "500 Internal Server Error", "{\"error\":\"out of memory\"}"); return; }
    size_t got = fread(body, 1, (size_t)sz, f);
    fclose(f);
    body[got] = '\0';
    send_response(fd, "200 OK", "text/html", body);
    free(body);
}

static void url_decode(char *out, size_t cap, const char *in) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < cap; i++) {
        if (in[i] == '+') {
            out[j++] = ' ';
        } else if (in[i] == '%' && isxdigit((unsigned char)in[i + 1]) && isxdigit((unsigned char)in[i + 2])) {
            char x[3] = { in[i + 1], in[i + 2], 0 };
            out[j++] = (char)strtol(x, NULL, 16);
            i += 2;
        } else {
            out[j++] = in[i];
        }
    }
    out[j] = '\0';
}

static void get_q(const char *path, char *out, size_t cap) {
    const char *q = strstr(path, "?q=");
    if (!q) { out[0] = '\0'; return; }
    q += 3;
    char tmp[512];
    size_t i = 0;
    while (q[i] && q[i] != ' ' && q[i] != '&' && i + 1 < sizeof(tmp)) {
        tmp[i] = q[i]; i++;
    }
    tmp[i] = '\0';
    url_decode(out, cap, tmp);
}

static void json_escape(char *out, size_t cap, const char *in) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < cap; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') { out[j++] = '\\'; out[j++] = (char)c; }
        else if (c == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
        else if (c == '\r') { out[j++] = '\\'; out[j++] = 'r'; }
        else if (c == '\t') { out[j++] = '\\'; out[j++] = 't'; }
        else if (c >= 0x20) out[j++] = (char)c;
    }
    out[j] = '\0';
}

static int parse_content_length(const char *req) {
    const char *p = req;
    while (*p) {
        if ((p[0] == 'C' || p[0] == 'c') &&
            (p[1] == 'o' || p[1] == 'O') &&
            (p[2] == 'n' || p[2] == 'N') &&
            (p[3] == 't' || p[3] == 'T') &&
            (p[4] == 'e' || p[4] == 'E') &&
            (p[5] == 'n' || p[5] == 'N') &&
            (p[6] == 't' || p[6] == 'T') &&
            p[7] == '-' &&
            (p[8] == 'L' || p[8] == 'l') &&
            (p[9] == 'e' || p[9] == 'E') &&
            (p[10] == 'n' || p[10] == 'N') &&
            (p[11] == 'g' || p[11] == 'G') &&
            (p[12] == 't' || p[12] == 'T') &&
            (p[13] == 'h' || p[13] == 'H') && p[14] == ':') break;
        p++;
    }
    if (!*p) return 0;
    if (!p) return 0;
    p += strlen("Content-Length:");
    while (*p == ' ' || *p == '\t') p++;
    return atoi(p);
}

static ssize_t read_request(int fd, char *buf, size_t cap) {
    size_t used = 0;
    size_t header_end = 0;
    int have_headers = 0;
    int content_length = 0;

    while (used + 1 < cap) {
        ssize_t n = recv(fd, buf + used, cap - used - 1, 0);
        if (n <= 0) break;
        used += (size_t)n;
        buf[used] = '\0';

        if (!have_headers) {
            char *end = strstr(buf, "\r\n\r\n");
            if (!end) continue;
            have_headers = 1;
            header_end = (size_t)(end - buf) + 4;
            content_length = parse_content_length(buf);
            if (content_length < 0 || (size_t)content_length > cap - header_end - 1) return -1;
        }
        if (have_headers && used >= header_end + (size_t)content_length) break;
    }
    return (ssize_t)used;
}

static int parse_json_query(const char *body, char *out, size_t cap) {
    const char *k = strstr(body, "\"query\"");
    if (!k) return 0;
    k = strchr(k, ':');
    if (!k) return 0;
    k++;
    while (*k && isspace((unsigned char)*k)) k++;
    if (*k != '"') return 0;
    k++;

    size_t j = 0;
    while (*k && *k != '"') {
        if (j + 1 >= cap) return 0;
        if (*k == '\\') {
            k++;
            if (!*k) return 0;
            switch (*k) {
                case '"': case '\\': case '/': out[j++] = *k; break;
                case 'n': out[j++] = '\n'; break;
                case 'r': out[j++] = '\r'; break;
                case 't': out[j++] = '\t'; break;
                default: return 0;
            }
        } else {
            out[j++] = *k;
        }
        k++;
    }
    if (*k != '"') return 0;
    out[j] = '\0';
    return j > 0;
}

static void seed_tree(void) {
    splay_free(&tree);
    splay_init(&tree);
    for (size_t i = 0; i < SEED_COUNT; i++) {
        int hit, depth, rotations;
        if (!splay_access(&tree, SEEDS[i], &hit, &depth, &rotations)) break;
    }
    /* Seed accesses are setup, not user searches. */
    tree.searches = 0;
    tree.hits = 0;
    splay_reset_counts(tree.root);
}

typedef struct { char *body; size_t pos; int first; } TreeCtx;

static void emit_tree(SplayNode *n, void *ctx) {
    TreeCtx *c = (TreeCtx *)ctx;
    char e[512], pe[512];
    json_escape(e, sizeof(e), n->key);
    if (n->parent) json_escape(pe, sizeof(pe), n->parent->key);

    int depth = 0;
    for (SplayNode *x = n; x && x->parent; x = x->parent) depth++;

    int written;
    if (n->parent) {
        written = snprintf(c->body + c->pos, JSON_CAP - c->pos,
            "%s{\"key\":\"%s\",\"count\":%d,\"depth\":%d,\"parent\":\"%s\"}",
            c->first ? "" : ",", e, n->count, depth, pe);
    } else {
        written = snprintf(c->body + c->pos, JSON_CAP - c->pos,
            "%s{\"key\":\"%s\",\"count\":%d,\"depth\":%d,\"parent\":null}",
            c->first ? "" : ",", e, n->count, depth);
    }
    if (written > 0 && (size_t)written < JSON_CAP - c->pos) c->pos += (size_t)written;
    c->first = 0;
}

static void send_tree(int fd) {
    char body[JSON_CAP];
    TreeCtx c = { body, 0, 1 };
    c.pos += (size_t)snprintf(body + c.pos, sizeof(body) - c.pos, "{\"nodes\":[");
    splay_foreach(tree.root, emit_tree, &c);
    c.pos += (size_t)snprintf(body + c.pos, sizeof(body) - c.pos, "]}");
    send_json(fd, "200 OK", body);
}

static int match_path(const char *path, const char *target) {
    size_t len = strlen(target);
    if (strncmp(path, target, len) != 0) return 0;
    return path[len] == '\0' || path[len] == '?';
}

static void handle(int fd, const char *req) {
    char method[8], path[1024];
    if (sscanf(req, "%7s %1023s", method, path) != 2) {
        send_json(fd, "400 Bad Request", "{\"error\":\"bad request\"}");
        return;
    }

    if (!strcmp(method, "OPTIONS")) { send_json(fd, "204 No Content", ""); return; }
    if (!strcmp(method, "GET") && (match_path(path, "/") || match_path(path, "/index.html"))) { send_file(fd, "web/index.html"); return; }

    if (!strcmp(method, "GET") && match_path(path, "/api/suggestions")) {
        char q[512];
        get_q(path, q, sizeof(q));
        const char *res[TRIE_MAX_RESULTS];
        int n = trie_complete(trie, q, res, 5);
        char body[4096], eq[1024];
        json_escape(eq, sizeof(eq), q);
        size_t p = (size_t)snprintf(body, sizeof(body), "{\"query\":\"%s\",\"suggestions\":[", eq);
        for (int i = 0; i < n && p < sizeof(body); i++) {
            char e[512]; json_escape(e, sizeof(e), res[i]);
            int w = snprintf(body + p, sizeof(body) - p, "%s\"%s\"", i ? "," : "", e);
            if (w > 0) p += (size_t)w;
        }
        snprintf(body + p, sizeof(body) - p, "]}");
        send_json(fd, "200 OK", body);
        return;
    }

    if (!strcmp(method, "GET") && match_path(path, "/api/tree")) { send_tree(fd); return; }

    if (!strcmp(method, "GET") && match_path(path, "/api/stats")) {
        char body[256];
        snprintf(body, sizeof(body), "{\"searches\":%lu,\"cache_hits\":%lu,\"hit_rate\":%.4f}",
            tree.searches, tree.hits, tree.searches ? (double)tree.hits / tree.searches : 0.0);
        send_json(fd, "200 OK", body);
        return;
    }

    if (!strcmp(method, "POST") && match_path(path, "/api/reset")) {
        seed_tree();
        send_json(fd, "200 OK", "{\"ok\":true}");
        return;
    }

    if (!strcmp(method, "POST") && match_path(path, "/api/search")) {
        const char *body = strstr(req, "\r\n\r\n");
        if (!body) { send_json(fd, "400 Bad Request", "{\"error\":\"missing body\"}"); return; }
        body += 4;
        char q[1024];
        if (!parse_json_query(body, q, sizeof(q))) {
            send_json(fd, "400 Bad Request", "{\"error\":\"expected JSON body with a non-empty query string\"}");
            return;
        }

        int hit, depth, rotations;
        SplayNode *n = splay_access(&tree, q, &hit, &depth, &rotations);
        if (!n) { send_json(fd, "500 Internal Server Error", "{\"error\":\"out of memory\"}"); return; }

        char e[2048], out[8192];
        json_escape(e, sizeof(e), q);
        snprintf(out, sizeof(out),
            "{\"query\":\"%s\",\"hit\":%s,\"depth\":%d,\"rotations\":%d,\"root\":\"%s\"}",
            e, hit ? "true" : "false", depth, rotations, e);
        send_json(fd, "200 OK", out);
        return;
    }

    send_json(fd, "404 Not Found", "{\"error\":\"not found\"}");
}

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    if (port < 1 || port > 65535) { fprintf(stderr, "Invalid port: %d\n", port); return 1; }

    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);
    signal(SIGPIPE, SIG_IGN);

    trie = trie_create();
    if (!trie) { fprintf(stderr, "Failed to create trie\n"); return 1; }

    const char *words =
        "intelligent interval interpreter internet interface integer integral integrate interrupt interactive internal intersect index input inline insert instance inherit inode "
        "algorithm array binary balance cache compile complexity data depth database graph hash heap lookup latency node query queue rotation recursion search splay sort stack tree trie token tokenizer transformer prefix probability pointer interpolation intermediate interleave interrogate interoperable intercept interlock internship intelligence intelligible intention intensity intent integrity integration inspect install instant instruction insight inquiry inbox increment incremental indent indexing inference infinite inflate influence information infrastructure initialize inject inheritance invariant inverse invert invoke iterator iteration abstraction accumulate adjacency allocate amortized analytics annotate append application approximate arithmetic assertion asynchronous attention automata autocomplete backtrack bandwidth benchmark bitmask boolean boundary breadth buffer bytecode callback checksum cluster compression concurrency constraint context convolution coordinate counter cursor decode decision dictionary dijkstra distributed dynamic embedding encode encryption entropy exception exponential fibonacci filter frequency function gradient greedy heuristic immutable kernel language lexer linear logarithm machine memory merge metric model network neural normalize optimization parser pipeline polynomial predict priority protocol pseudocode random regression resolver runtime scheduler semantic sequence serialize similarity sparse suffix syntax tensor traversal unicode vector vocabulary weight "
        "markov bayesian belief learning reinforcement critic actor deep recurrent convolutional lstm attention vision perception robotics planning ontology semantics representation reasoning theorem solver planner minimax pruning heuristic classifier clustering regression dimensionality reduction pca svd tsne umap embeddings layer activation relu sigmoid tanh softmax optimizer sgd adam rmsprop crossentropy loss regularizer dropout batchnorm fine tuning pretraining distillation alignment safety ethical bias fairness interpretability explainability generative autoregressive autoencoder gan diffusion vae prompt context rag generation retrieval synthesis dialog dialogue bot agent multiagent simulation environment observation reward state action transition terminal episode horizon monte carlo td value policy q learning sarsa td lambda eligibility traces prior posterior likelihood marginal conditional joint independence markov blanket message passing belief propagation exact approximate variational inference sampling mcmc gibbs metropolis hastings particle filter hiddem hmm kalman filter smoothing filtering prediction viterbi forward backward baum welch logic proposition predicate first order resolution unification resolution clause horn resolution prolog datalog rdf owl sparql dl description knowledge graph triple store linked semantic web ontology inference engine rules backward chaining forward chaining production rete pattern match expert system cyc wordnet conceptnet dbpedia wikidata freebase yago";

    char tmp[8192];
    snprintf(tmp, sizeof(tmp), "%s", words);
    for (char *p = strtok(tmp, " "); p; p = strtok(NULL, " ")) {
        if (!trie_insert(trie, p)) { fprintf(stderr, "Failed to insert trie word: %s\n", p); trie_free(trie); return 1; }
    }

    splay_init(&tree);
    seed_tree();

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); trie_free(trie); splay_free(&tree); return 1; }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)port);

    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(s, 16) < 0) {
        perror("bind/listen"); close(s); trie_free(trie); splay_free(&tree); return 1;
    }

    printf("Splay backend listening on http://localhost:%d\n", port);
    fflush(stdout);

    while (running) {
        int c = accept(s, NULL, NULL);
        if (c < 0) { if (errno == EINTR) continue; break; }
        
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        
        char req[REQUEST_CAP];
        ssize_t n = read_request(c, req, sizeof(req));
        if (n > 0) handle(c, req);
        else if (n < 0) send_json(c, "413 Payload Too Large", "{\"error\":\"request too large\"}");
        close(c);
    }

    close(s);
    trie_free(trie);
    splay_free(&tree);
    return 0;
}
