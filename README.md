# Splay Type-ahead — Full Stack C Backend

This version keeps the supplied Splay Type-ahead UI concept but moves the authoritative trie/autocomplete and splay-tree state into a C backend.

## Requirements

- GCC or Clang
- POSIX system (Linux/macOS)
- `curl` is useful for API testing

## Build and run

```bash
make clean
make
./splay_server 8080
```

Open `http://localhost:8080/` in a browser.

The C server serves the frontend and REST API from the same origin, so no separate frontend server or CORS configuration is required.

## API

- `GET /api/suggestions?q=inte` — trie autocomplete (up to 5 results)
- `POST /api/search` with `{ "query": "integer overflow" }` — splay access
- `GET /api/stats` — searches, cache hits and hit rate
- `GET /api/tree` — current tree nodes for visualization
- `POST /api/reset` — restores the seven starter phrases and clears user statistics

## Starter phrases

The backend starts with the same seven starter phrases used by the supplied frontend:

- intelligent agents
- interval tree
- interpreter design
- internet speed
- integer overflow
- input latency
- index fund

Their setup operations are excluded from the displayed search/hit statistics.

## Architecture

```text
Browser UI
   |
   | HTTP/JSON
   v
C HTTP server
   |------------------|
   v                  v
Trie              Splay Tree
autocomplete      search cache
   |                  |
   +-------- JSON ----+
```

The search state is in the C process. Restarting the server restores the starter tree.
