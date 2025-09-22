#define _POSIX_C_SOURCE 200809L
#include "sync.h"
#include "chainio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#ifndef _WIN32

#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>

#define SYNC_PROTOCOL "SYNC/1"

typedef enum {
    REMOTE_STATUS_OK = 0,
    REMOTE_STATUS_FOREIGN = 1,
    REMOTE_STATUS_CONFLICT = 2
} remote_status_t;

typedef struct {
    const kolibri_config_t* cfg;
    char chain_path[256];
    int listen_fd;
    int port;
    int running;
    pthread_t thread;
} KolibriSyncService;

static KolibriSyncService g_service = {0};
static pthread_mutex_t g_service_lock = PTHREAD_MUTEX_INITIALIZER;

static void ensure_directory(const char* path){
    struct stat st;
    if(stat(path, &st) == 0){
        return;
    }
    mkdir(path, 0755);
}

static void sync_log(const char* fmt, ...){
    ensure_directory("logs");
    FILE* f = fopen("logs/sync.log", "ab");
    if(!f){
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void sanitize_node(const char* in, char* out, size_t n){
    size_t j = 0;
    for(size_t i = 0; in && in[i] && j + 1 < n; ++i){
        unsigned char c = (unsigned char)in[i];
        if(isalnum(c) || c == '-' || c == '_'){
            out[j++] = (char)c;
        } else {
            out[j++] = '_';
        }
    }
    if(j < n){
        out[j] = 0;
    }
}

static ssize_t read_line(int fd, char* buf, size_t n){
    size_t off = 0;
    while(off + 1 < n){
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if(r <= 0){
            return -1;
        }
        if(c == '\n'){
            break;
        }
        buf[off++] = c;
    }
    buf[off] = 0;
    return (ssize_t)off;
}

static bool send_line(int fd, const char* line){
    size_t len = strlen(line);
    if(send(fd, line, len, 0) != (ssize_t)len){
        return false;
    }
    if(send(fd, "\n", 1, 0) != 1){
        return false;
    }
    return true;
}

static bool read_exact(int fd, char* buf, size_t len){
    size_t off = 0;
    while(off < len){
        ssize_t r = recv(fd, buf + off, len - off, 0);
        if(r <= 0){
            return false;
        }
        off += (size_t)r;
    }
    return true;
}

static int open_listener(int port){
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if(fd < 0){
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if(fd < 0){
            return -1;
        }
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if(port <= 0){
        close(fd);
        return -1;
    }
    if(fd >= 0){
        if(port > 0){
            struct sockaddr_in6 addr6;
            memset(&addr6, 0, sizeof(addr6));
            addr6.sin6_family = AF_INET6;
            addr6.sin6_addr = in6addr_any;
            addr6.sin6_port = htons((uint16_t)port);
            if(bind(fd, (struct sockaddr*)&addr6, sizeof(addr6)) == 0){
                if(listen(fd, 8) == 0){
                    return fd;
                }
            }
            close(fd);
        }
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0){
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr4;
    memset(&addr4, 0, sizeof(addr4));
    addr4.sin_family = AF_INET;
    addr4.sin_addr.s_addr = htonl(INADDR_ANY);
    addr4.sin_port = htons((uint16_t)port);
    if(bind(fd, (struct sockaddr*)&addr4, sizeof(addr4)) != 0){
        close(fd);
        return -1;
    }
    if(listen(fd, 8) != 0){
        close(fd);
        return -1;
    }
    return fd;
}

static int connect_peer(const kolibri_peer_t* peer){
    if(!peer || !peer->host[0] || peer->port <= 0){
        return -1;
    }
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", peer->port);
    struct addrinfo* res = NULL;
    if(getaddrinfo(peer->host, portbuf, &hints, &res) != 0){
        return -1;
    }
    int fd = -1;
    for(struct addrinfo* it = res; it; it = it->ai_next){
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if(fd < 0){
            continue;
        }
        if(connect(fd, it->ai_addr, it->ai_addrlen) == 0){
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

typedef struct {
    int fd;
} stream_ctx_t;

static bool stream_block_cb(const char* line, const ReasonBlock* block, void* user){
    (void)block;
    stream_ctx_t* ctx = (stream_ctx_t*)user;
    size_t len = strlen(line);
    char header[128];
    snprintf(header, sizeof(header), "BLOCK %llu %zu", (unsigned long long)block->step, len);
    if(!send_line(ctx->fd, header)){
        return false;
    }
    if(send(ctx->fd, line, len, 0) != (ssize_t)len){
        return false;
    }
    if(send(ctx->fd, "\n", 1, 0) != 1){
        return false;
    }
    return true;
}

static void handle_connection(int fd, KolibriSyncService* svc){
    char line[1024];
    char remote_node[64] = {0};
    char remote_fp[65] = {0};
    unsigned long long request_from = 1ULL;

    while(true){
        ssize_t got = read_line(fd, line, sizeof(line));
        if(got < 0){
            return;
        }
        if(strcmp(line, "END") == 0){
            break;
        }
        if(strcmp(line, SYNC_PROTOCOL) == 0){
            continue;
        }
        if(strncmp(line, "NODE ", 5) == 0){
            snprintf(remote_node, sizeof(remote_node), "%s", line + 5);
            continue;
        }
        if(strncmp(line, "FP ", 3) == 0){
            snprintf(remote_fp, sizeof(remote_fp), "%s", line + 3);
            continue;
        }
        if(strncmp(line, "REQUEST ", 8) == 0){
            request_from = strtoull(line + 8, NULL, 10);
            continue;
        }
    }

    kolibri_chain_summary_t summary;
    if(!chain_get_summary(svc->chain_path, &summary, svc->cfg)){
        return;
    }

    remote_status_t status = REMOTE_STATUS_OK;
    if(remote_fp[0] && summary.fingerprint[0] && strcmp(remote_fp, summary.fingerprint) != 0){
        status = REMOTE_STATUS_FOREIGN;
    }

    send_line(fd, SYNC_PROTOCOL);
    if(status == REMOTE_STATUS_FOREIGN){
        send_line(fd, "STATUS foreign");
    } else {
        send_line(fd, "STATUS ok");
    }
    char buf[256];
    const char* node_id = svc->cfg->node_id[0] ? svc->cfg->node_id : "";
    snprintf(buf, sizeof(buf), "NODE %s", node_id);
    send_line(fd, buf);
    snprintf(buf, sizeof(buf), "FP %s", summary.fingerprint);
    send_line(fd, buf);
    snprintf(buf, sizeof(buf), "HEIGHT %llu", (unsigned long long)summary.height);
    send_line(fd, buf);
    snprintf(buf, sizeof(buf), "HASH %s", summary.head_hash);
    send_line(fd, buf);

    if(summary.height > 0 && request_from <= summary.height){
        stream_ctx_t ctx = {.fd = fd};
        if(!chain_stream_from(svc->chain_path, (uint64_t)request_from, stream_block_cb, &ctx)){
            sync_log("[sync] stream error while serving peer");
        }
    }
    send_line(fd, "END");

    (void)remote_node;
}

static void* sync_thread_main(void* arg){
    (void)arg;
    while(1){
        pthread_mutex_lock(&g_service_lock);
        int running = g_service.running;
        int listen_fd = g_service.listen_fd;
        pthread_mutex_unlock(&g_service_lock);
        if(!running || listen_fd < 0){
            break;
        }
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        int fd = accept(listen_fd, (struct sockaddr*)&ss, &slen);
        if(fd < 0){
            if(errno == EINTR){
                continue;
            }
            if(!running){
                break;
            }
            continue;
        }
        handle_connection(fd, &g_service);
        close(fd);
    }
    return NULL;
}

typedef struct {
    char node_id[64];
    char fingerprint[65];
    char head_hash[65];
    unsigned long long height;
} peer_summary_t;

static bool sync_with_peer(const kolibri_config_t* cfg, const char* chain_path, const kolibri_peer_t* peer, kolibri_chain_summary_t* summary){
    int fd = connect_peer(peer);
    if(fd < 0){
        sync_log("[sync] unable to connect %s:%d", peer->host, peer->port);
        return false;
    }

    const char* node_id = cfg->node_id[0] ? cfg->node_id : "";
    char buf[256];
    send_line(fd, SYNC_PROTOCOL);
    snprintf(buf, sizeof(buf), "NODE %s", node_id);
    send_line(fd, buf);
    snprintf(buf, sizeof(buf), "FP %s", summary->fingerprint);
    send_line(fd, buf);
    snprintf(buf, sizeof(buf), "HEIGHT %llu", (unsigned long long)summary->height);
    send_line(fd, buf);
    snprintf(buf, sizeof(buf), "HASH %s", summary->head_hash);
    send_line(fd, buf);
    snprintf(buf, sizeof(buf), "REQUEST %llu", (unsigned long long)(summary->height + 1));
    send_line(fd, buf);
    snprintf(buf, sizeof(buf), "TRUST %.3f", cfg->sync_trust_ratio);
    send_line(fd, buf);
    send_line(fd, "END");

    peer_summary_t remote = {0};
    remote_status_t status = REMOTE_STATUS_OK;
    bool headers_received = false;
    char line[1024];
    bool ok = true;
    char prev_hash[65];
    snprintf(prev_hash, sizeof(prev_hash), "%s", summary->head_hash);
    uint64_t expected_step = summary->height + 1;
    size_t appended = 0;
    FILE* stash = NULL;
    char stash_path[256] = {0};

    while(true){
        ssize_t got = read_line(fd, line, sizeof(line));
        if(got < 0){
            ok = false;
            break;
        }
        if(strcmp(line, SYNC_PROTOCOL) == 0){
            headers_received = true;
            continue;
        }
        if(strncmp(line, "STATUS ", 7) == 0){
            if(strstr(line + 7, "foreign")){
                status = REMOTE_STATUS_FOREIGN;
            }
            continue;
        }
        if(strncmp(line, "NODE ", 5) == 0){
            snprintf(remote.node_id, sizeof(remote.node_id), "%s", line + 5);
            continue;
        }
        if(strncmp(line, "FP ", 3) == 0){
            snprintf(remote.fingerprint, sizeof(remote.fingerprint), "%s", line + 3);
            continue;
        }
        if(strncmp(line, "HEIGHT ", 7) == 0){
            remote.height = strtoull(line + 7, NULL, 10);
            continue;
        }
        if(strncmp(line, "HASH ", 5) == 0){
            snprintf(remote.head_hash, sizeof(remote.head_hash), "%s", line + 5);
            continue;
        }
        if(strcmp(line, "END") == 0){
            break;
        }
        if(strncmp(line, "BLOCK ", 6) == 0){
            if(!headers_received){
                ok = false;
                break;
            }
            char* saveptr = NULL;
            char* token = strtok_r(line + 6, " ", &saveptr);
            if(!token){
                ok = false;
                break;
            }
            uint64_t step = (uint64_t)strtoull(token, NULL, 10);
            token = strtok_r(NULL, " ", &saveptr);
            if(!token){
                ok = false;
                break;
            }
            size_t payload_len = (size_t)strtoull(token, NULL, 10);
            if(payload_len == 0 || payload_len >= 8190){
                ok = false;
                break;
            }
            char payload[8192];
            if(!read_exact(fd, payload, payload_len)){
                ok = false;
                break;
            }
            payload[payload_len] = 0;
            char newline;
            if(recv(fd, &newline, 1, 0) <= 0){
                ok = false;
                break;
            }
            ReasonBlock block;
            if(!chain_parse_line(payload, &block)){
                ok = false;
                break;
            }
            bool fingerprint_mismatch = false;
            if(block.config_fingerprint[0] && summary->fingerprint[0] && strcmp(block.config_fingerprint, summary->fingerprint) != 0){
                fingerprint_mismatch = true;
            }
            bool treat_as_foreign = (status == REMOTE_STATUS_FOREIGN) || fingerprint_mismatch;
            bool should_append = !treat_as_foreign && step == expected_step;
            if(should_append){
                if(!chain_validate_block(&block, cfg, prev_hash)){
                    should_append = false;
                }
            }
            if(should_append){
                if(!chain_append(chain_path, &block, cfg)){
                    ok = false;
                    break;
                }
                snprintf(prev_hash, sizeof(prev_hash), "%s", block.hash);
                expected_step = step + 1;
                appended++;
                summary->height = block.step;
                snprintf(summary->head_hash, sizeof(summary->head_hash), "%s", block.hash);
                snprintf(summary->prev_hash, sizeof(summary->prev_hash), "%s", block.prev);
                if(block.config_fingerprint[0]){
                    snprintf(summary->fingerprint, sizeof(summary->fingerprint), "%s", block.config_fingerprint);
                }
            } else {
                if(!stash){
                    ensure_directory("logs");
                    if(treat_as_foreign){
                        ensure_directory("logs/foreign");
                    } else {
                        ensure_directory("logs/conflicts");
                    }
                    char safe[64];
                    const char* remote_id = remote.node_id[0] ? remote.node_id : peer->host;
                    sanitize_node(remote_id, safe, sizeof(safe));
                    if(treat_as_foreign){
                        snprintf(stash_path, sizeof(stash_path), "logs/foreign/%s.jsonl", safe);
                    } else {
                        snprintf(stash_path, sizeof(stash_path), "logs/conflicts/%s.jsonl", safe);
                    }
                    stash = fopen(stash_path, "ab");
                }
                if(stash){
                    fprintf(stash, "%s\n", payload);
                }
            }
        }
    }

    if(stash){
        fclose(stash);
        if(status == REMOTE_STATUS_FOREIGN){
            sync_log("[sync] stored foreign blocks from %s at %s", peer->host, stash_path);
        } else {
            sync_log("[sync] stored conflicting blocks from %s at %s", peer->host, stash_path);
        }
    }

    close(fd);

    if(appended > 0){
        sync_log("[sync] pulled %zu blocks from %s", appended, peer->host);
    }

    return ok;
}

bool kolibri_sync_service_start(const kolibri_config_t* cfg, const char* chain_path){
    if(!cfg || !cfg->sync_enabled || cfg->sync_listen_port <= 0){
        return true;
    }
    if(!chain_path){
        return false;
    }
    pthread_mutex_lock(&g_service_lock);
    if(g_service.running){
        pthread_mutex_unlock(&g_service_lock);
        return true;
    }
    g_service.cfg = cfg;
    snprintf(g_service.chain_path, sizeof(g_service.chain_path), "%s", chain_path);
    g_service.port = cfg->sync_listen_port;
    g_service.listen_fd = open_listener(cfg->sync_listen_port);
    if(g_service.listen_fd < 0){
        pthread_mutex_unlock(&g_service_lock);
        sync_log("[sync] failed to listen on %d", cfg->sync_listen_port);
        return false;
    }
    g_service.running = 1;
    pthread_mutex_unlock(&g_service_lock);
    if(pthread_create(&g_service.thread, NULL, sync_thread_main, NULL) != 0){
        pthread_mutex_lock(&g_service_lock);
        close(g_service.listen_fd);
        g_service.listen_fd = -1;
        g_service.running = 0;
        pthread_mutex_unlock(&g_service_lock);
        sync_log("[sync] failed to spawn sync thread");
        return false;
    }
    sync_log("[sync] service listening on %d", cfg->sync_listen_port);
    return true;
}

void kolibri_sync_service_stop(void){
    pthread_mutex_lock(&g_service_lock);
    int running = g_service.running;
    int listen_fd = g_service.listen_fd;
    g_service.running = 0;
    g_service.listen_fd = -1;
    pthread_mutex_unlock(&g_service_lock);
    if(listen_fd >= 0){
        close(listen_fd);
    }
    if(running){
        pthread_join(g_service.thread, NULL);
    }
}

bool kolibri_sync_tick(const kolibri_config_t* cfg, const char* chain_path){
    if(!cfg || !cfg->sync_enabled || kolibri_config_peer_count(cfg) == 0){
        return true;
    }
    kolibri_chain_summary_t summary;
    if(!chain_get_summary(chain_path, &summary, cfg)){
        sync_log("[sync] unable to summarize chain");
        return false;
    }
    if(!summary.fingerprint[0] && cfg->fingerprint[0]){
        snprintf(summary.fingerprint, sizeof(summary.fingerprint), "%s", cfg->fingerprint);
    }
    bool ok = true;
    for(size_t i = 0; i < kolibri_config_peer_count(cfg); ++i){
        const kolibri_peer_t* peer = kolibri_config_peer(cfg, i);
        if(!peer){
            continue;
        }
        if(!sync_with_peer(cfg, chain_path, peer, &summary)){
            ok = false;
        }
    }
    return ok;
}

#else

bool kolibri_sync_service_start(const kolibri_config_t* cfg, const char* chain_path){
    (void)cfg;
    (void)chain_path;
    return true;
}

void kolibri_sync_service_stop(void){
}

bool kolibri_sync_tick(const kolibri_config_t* cfg, const char* chain_path){
    (void)cfg;
    (void)chain_path;
    return true;
}

#endif
