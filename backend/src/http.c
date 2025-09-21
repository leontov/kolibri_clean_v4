#include "http.h"

#include "fmt_v5.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_SSE_CLIENTS 16

typedef struct {
    kolibri_runtime_t *runtime;
    kolibri_chain_t *chain;
    kolibri_http_config_t config;
    pthread_mutex_t chain_mutex;
} http_server_ctx_t;

typedef struct {
    FILE *stream;
    int fd;
    int active;
} sse_client_t;

typedef struct {
    http_server_ctx_t *ctx;
    int client_fd;
} connection_payload_t;

static sse_client_t sse_clients[MAX_SSE_CLIENTS];
static pthread_mutex_t sse_mutex = PTHREAD_MUTEX_INITIALIZER;

static void sse_remove_locked(int idx) {
    if (idx < 0 || idx >= MAX_SSE_CLIENTS) {
        return;
    }
    if (!sse_clients[idx].active) {
        return;
    }
    if (sse_clients[idx].stream) {
        fclose(sse_clients[idx].stream);
    }
    sse_clients[idx].stream = NULL;
    sse_clients[idx].fd = -1;
    sse_clients[idx].active = 0;
}

static int sse_add_client(FILE *stream, int fd) {
    pthread_mutex_lock(&sse_mutex);
    int idx = -1;
    for (int i = 0; i < MAX_SSE_CLIENTS; ++i) {
        if (!sse_clients[i].active) {
            sse_clients[i].active = 1;
            sse_clients[i].stream = stream;
            sse_clients[i].fd = fd;
            idx = i;
            break;
        }
    }
    pthread_mutex_unlock(&sse_mutex);
    return idx;
}

static void sse_remove_client(int idx) {
    pthread_mutex_lock(&sse_mutex);
    sse_remove_locked(idx);
    pthread_mutex_unlock(&sse_mutex);
}

static void sse_broadcast(const char *event, const char *json) {
    pthread_mutex_lock(&sse_mutex);
    for (int i = 0; i < MAX_SSE_CLIENTS; ++i) {
        if (!sse_clients[i].active || !sse_clients[i].stream) {
            continue;
        }
        FILE *stream = sse_clients[i].stream;
        if (fprintf(stream, "event: %s\n", event) < 0) {
            sse_remove_locked(i);
            continue;
        }
        if (fprintf(stream, "data: %s\n\n", json) < 0) {
            sse_remove_locked(i);
            continue;
        }
        if (fflush(stream) != 0) {
            sse_remove_locked(i);
        }
    }
    pthread_mutex_unlock(&sse_mutex);
}

static void http_sse_callback(const char *event, const char *json, void *user_data) {
    (void)user_data;
    sse_broadcast(event, json);
}

static void respond_json(FILE *stream, const char *body, int status, int cors_dev) {
    fprintf(stream, "HTTP/1.1 %d OK\r\n", status);
    fprintf(stream, "Content-Type: application/json\r\n");
    if (cors_dev) {
        fprintf(stream, "Access-Control-Allow-Origin: *\r\n");
    }
    fprintf(stream, "Cache-Control: no-cache\r\n");
    fprintf(stream, "Content-Length: %zu\r\n\r\n", strlen(body));
    fprintf(stream, "%s", body);
    fflush(stream);
}

static void respond_text(FILE *stream, const char *body, int status, const char *content_type, int cors_dev) {
    fprintf(stream, "HTTP/1.1 %d OK\r\n", status);
    fprintf(stream, "Content-Type: %s\r\n", content_type);
    if (cors_dev) {
        fprintf(stream, "Access-Control-Allow-Origin: *\r\n");
    }
    fprintf(stream, "Content-Length: %zu\r\n\r\n", strlen(body));
    fprintf(stream, "%s", body);
    fflush(stream);
}

static void respond_file(FILE *stream, const char *path, int cors_dev, const char *fallback) {
    FILE *fp = fopen(path, "rb");
    if (!fp && fallback) {
        fp = fopen(fallback, "rb");
        path = fallback;
    }
    if (!fp) {
        respond_text(stream, "Not Found", 404, "text/plain", cors_dev);
        return;
    }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    rewind(fp);
    char *buffer = (char *)malloc((size_t)len);
    if (!buffer) {
        fclose(fp);
        respond_text(stream, "Internal Server Error", 500, "text/plain", cors_dev);
        return;
    }
    size_t read = fread(buffer, 1, (size_t)len, fp);
    fclose(fp);
    const char *ext = strrchr(path, '.');
    const char *mime = "text/plain";
    if (ext) {
        if (strcmp(ext, ".html") == 0) {
            mime = "text/html";
        } else if (strcmp(ext, ".js") == 0) {
            mime = "application/javascript";
        } else if (strcmp(ext, ".css") == 0) {
            mime = "text/css";
        } else if (strcmp(ext, ".json") == 0) {
            mime = "application/json";
        } else if (strcmp(ext, ".png") == 0) {
            mime = "image/png";
        } else if (strcmp(ext, ".svg") == 0) {
            mime = "image/svg+xml";
        }
    }
    fprintf(stream, "HTTP/1.1 200 OK\r\n");
    fprintf(stream, "Content-Type: %s\r\n", mime);
    if (cors_dev) {
        fprintf(stream, "Access-Control-Allow-Origin: *\r\n");
    }
    fprintf(stream, "Content-Length: %zu\r\n\r\n", read);
    fwrite(buffer, 1, read, stream);
    fflush(stream);
    free(buffer);
}

static void respond_options(FILE *stream, int cors_dev) {
    fprintf(stream, "HTTP/1.1 204 No Content\r\n");
    fprintf(stream, "Allow: GET,POST,OPTIONS\r\n");
    if (cors_dev) {
        fprintf(stream, "Access-Control-Allow-Origin: *\r\n");
        fprintf(stream, "Access-Control-Allow-Headers: Content-Type\r\n");
    }
    fprintf(stream, "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n\r\n");
    fflush(stream);
}

static void build_status_json(const http_server_ctx_t *ctx, char *out, size_t out_len) {
    time_t now = time(NULL);
    struct tm gmt;
    gmtime_r(&now, &gmt);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", &gmt);
    snprintf(out, out_len,
             "{\"version\":\"%s\",\"run_id\":\"%s\",\"time_utc\":\"%s\",\"fmt\":%u}",
             KOLIBRI_VERSION, ctx->runtime->config.run_id, time_buf, ctx->runtime->config.fmt_default);
}

static void build_chain_json(http_server_ctx_t *ctx, size_t tail, char **out_body) {
    kolibri_chain_t subset;
    kolibri_chain_init(&subset);
    pthread_mutex_lock(&ctx->chain_mutex);
    kolibri_chain_tail_copy(ctx->chain, tail, &subset);
    pthread_mutex_unlock(&ctx->chain_mutex);
    size_t capacity = 4096;
    char *body = (char *)malloc(capacity);
    size_t offset = 0;
    offset += snprintf(body + offset, capacity - offset, "[");
    for (size_t i = 0; i < subset.count; ++i) {
        char payload_json[4096];
        size_t len = fmt_payload_json(&subset.blocks[i].payload, payload_json, sizeof(payload_json));
        if (offset + len + 64 >= capacity) {
            capacity *= 2;
            body = (char *)realloc(body, capacity);
        }
        offset += snprintf(body + offset, capacity - offset, "%s{\"payload\":%.*s,\"hash\":\"%s\",\"hmac\":\"%s\"}",
                           (i == 0) ? "" : ",", (int)len, payload_json, subset.blocks[i].hash, subset.blocks[i].hmac);
    }
    offset += snprintf(body + offset, capacity - offset, "]");
    kolibri_chain_free(&subset);
    *out_body = body;
}

typedef struct {
    http_server_ctx_t *ctx;
    unsigned steps;
    unsigned beam;
    double lambda;
    unsigned fmt;
    uint64_t seed;
} run_request_t;

static void *run_worker(void *arg) {
    run_request_t *req = (run_request_t *)arg;
    http_server_ctx_t *ctx = req->ctx;
    pthread_mutex_lock(&ctx->chain_mutex);
    kolibri_chain_free(ctx->chain);
    kolibri_chain_init(ctx->chain);
    uint64_t prev_seed = ctx->runtime->config.seed_default;
    double prev_lambda = ctx->runtime->config.lambda_default;
    unsigned prev_fmt = ctx->runtime->config.fmt_default;
    ctx->runtime->config.seed_default = req->seed;
    ctx->runtime->config.lambda_default = req->lambda;
    ctx->runtime->config.fmt_default = req->fmt;
    kolibri_run_with_callback(ctx->runtime, req->steps, req->beam, req->lambda, req->fmt, KOLIBRI_DEFAULT_LOG, ctx->chain, http_sse_callback, ctx);
    ctx->runtime->config.seed_default = prev_seed;
    ctx->runtime->config.lambda_default = prev_lambda;
    ctx->runtime->config.fmt_default = prev_fmt;
    pthread_mutex_unlock(&ctx->chain_mutex);
    free(req);
    return NULL;
}

static void serve_run(http_server_ctx_t *ctx, FILE *stream, const char *body) {
    unsigned steps = 30;
    unsigned fmt = ctx->runtime->config.fmt_default;
    double lambda = ctx->runtime->config.lambda_default;
    uint64_t seed = ctx->runtime->config.seed_default;
    unsigned beam = 1;
    if (body) {
        sscanf(body, "{\"steps\":%u", &steps);
        sscanf(body, "%*[^\"seed\"]\"seed\":%llu", (unsigned long long *)&seed);
        sscanf(body, "%*[^\"lambda\"]\"lambda\":%lf", &lambda);
        sscanf(body, "%*[^\"fmt\"]\"fmt\":%u", &fmt);
    }
    run_request_t *req = (run_request_t *)calloc(1, sizeof(run_request_t));
    req->ctx = ctx;
    req->steps = steps;
    req->beam = beam;
    req->lambda = lambda;
    req->fmt = fmt;
    req->seed = seed;
    pthread_t thread;
    pthread_create(&thread, NULL, run_worker, req);
    pthread_detach(thread);
    char response[320];
    snprintf(response, sizeof(response), "{\"started\":true,\"steps\":%u}", steps);
    respond_json(stream, response, 200, ctx->config.cors_dev);
}

static void serve_verify(http_server_ctx_t *ctx, FILE *stream, const char *body) {
    (void)ctx;
    char path[256] = KOLIBRI_DEFAULT_LOG;
    if (body) {
        sscanf(body, "%*[^\"path\"]\"path\":\"%255[^\"]\"", path);
    }
    int rc = kolibri_verify_file(path, 0);
    char response[320];
    snprintf(response, sizeof(response), "{\"ok\":%s,\"path\":\"%s\"}", rc == 0 ? "true" : "false", path);
    respond_json(stream, response, rc == 0 ? 200 : 400, ctx->config.cors_dev);
}

static void serve_skills(FILE *stream, int cors_dev) {
    const char *body = "{\"skills\":[{\"id\":\"demo\",\"name\":\"Kolibri Demo Skill\",\"status\":\"beta\"}]}";
    respond_json(stream, body, 200, cors_dev);
}

static void serve_chain(http_server_ctx_t *ctx, FILE *stream, const char *path) {
    size_t tail = 10;
    const char *q = strchr(path, '?');
    if (q) {
        unsigned t = 0;
        if (sscanf(q, "?tail=%u", &t) == 1) {
            tail = t;
        }
    }
    char *body = NULL;
    build_chain_json(ctx, tail, &body);
    respond_json(stream, body, 200, ctx->config.cors_dev);
    free(body);
}

static void serve_status(http_server_ctx_t *ctx, FILE *stream) {
    char body[256];
    build_status_json(ctx, body, sizeof(body));
    respond_json(stream, body, 200, ctx->config.cors_dev);
}

static void serve_sse(http_server_ctx_t *ctx, int client_fd, FILE *stream) {
    (void)ctx;
    fprintf(stream, "HTTP/1.1 200 OK\r\n");
    fprintf(stream, "Content-Type: text/event-stream\r\n");
    fprintf(stream, "Cache-Control: no-cache\r\n");
    fprintf(stream, "Connection: keep-alive\r\n");
    if (ctx->config.cors_dev) {
        fprintf(stream, "Access-Control-Allow-Origin: *\r\n");
    }
    fprintf(stream, "\r\n");
    fflush(stream);
    int idx = sse_add_client(stream, client_fd);
    if (idx < 0) {
        fprintf(stream, "event: error\n");
        fprintf(stream, "data: {\"reason\":\"too many clients\"}\n\n");
        fflush(stream);
        fclose(stream);
        return;
    }
    while (1) {
        sleep(15);
        pthread_mutex_lock(&sse_mutex);
        if (!sse_clients[idx].active) {
            pthread_mutex_unlock(&sse_mutex);
            break;
        }
        if (fprintf(sse_clients[idx].stream, ": keep-alive\n\n") < 0) {
            sse_remove_locked(idx);
            pthread_mutex_unlock(&sse_mutex);
            break;
        }
        fflush(sse_clients[idx].stream);
        pthread_mutex_unlock(&sse_mutex);
    }
    sse_remove_client(idx);
}

static void *connection_thread(void *arg) {
    connection_payload_t *payload = (connection_payload_t *)arg;
    http_server_ctx_t *ctx = payload->ctx;
    int client_fd = payload->client_fd;
    free(payload);
    FILE *stream = fdopen(client_fd, "r+");
    if (!stream) {
        close(client_fd);
        return NULL;
    }
    setvbuf(stream, NULL, _IONBF, 0);
    char request_line[1024];
    if (!fgets(request_line, sizeof(request_line), stream)) {
        fclose(stream);
        return NULL;
    }
    char method[8];
    char path[512];
    sscanf(request_line, "%7s %511s", method, path);
    char header_line[1024];
    size_t content_length = 0;
    while (fgets(header_line, sizeof(header_line), stream)) {
        if (strcmp(header_line, "\r\n") == 0 || strcmp(header_line, "\n") == 0) {
            break;
        }
        if (strncasecmp(header_line, "Content-Length:", 15) == 0) {
            content_length = strtoul(header_line + 15, NULL, 10);
        }
    }
    char *body = NULL;
    if (content_length > 0) {
        body = (char *)malloc(content_length + 1);
        fread(body, 1, content_length, stream);
        body[content_length] = '\0';
    }
    if (strcmp(method, "OPTIONS") == 0) {
        respond_options(stream, ctx->config.cors_dev);
    } else if (strcmp(method, "GET") == 0) {
        if (strncmp(path, "/api/v1/status", 15) == 0) {
            serve_status(ctx, stream);
        } else if (strncmp(path, "/api/v1/chain/stream", 21) == 0) {
            free(body);
            serve_sse(ctx, client_fd, stream);
            return NULL;
        } else if (strncmp(path, "/api/v1/chain", 14) == 0) {
            serve_chain(ctx, stream, path);
        } else if (strncmp(path, "/api/v1/skills", 15) == 0) {
            serve_skills(stream, ctx->config.cors_dev);
        } else {
            char static_path[1024];
            snprintf(static_path, sizeof(static_path), "%s%s", ctx->config.static_root, strcmp(path, "/") == 0 ? "/index.html" : path);
            char fallback[1024];
            snprintf(fallback, sizeof(fallback), "%s/index.html", ctx->config.static_root);
            respond_file(stream, static_path, ctx->config.cors_dev, fallback);
        }
    } else if (strcmp(method, "POST") == 0) {
        if (strncmp(path, "/api/v1/run", 12) == 0) {
            serve_run(ctx, stream, body);
        } else if (strncmp(path, "/api/v1/verify", 15) == 0) {
            serve_verify(ctx, stream, body);
        } else {
            respond_text(stream, "Not Found", 404, "text/plain", ctx->config.cors_dev);
        }
    } else {
        respond_text(stream, "Method Not Allowed", 405, "text/plain", ctx->config.cors_dev);
    }
    free(body);
    fclose(stream);
    return NULL;
}

int kolibri_http_serve(kolibri_runtime_t *rt,
                       kolibri_chain_t *chain,
                       const kolibri_http_config_t *config) {
    if (!rt || !chain || !config) {
        return -1;
    }
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)config->port);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }
    if (listen(server_fd, 16) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }
    http_server_ctx_t ctx;
    ctx.runtime = rt;
    ctx.chain = chain;
    ctx.config = *config;
    pthread_mutex_init(&ctx.chain_mutex, NULL);

    printf("kolibri serve listening on port %d\n", config->port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        connection_payload_t *payload = (connection_payload_t *)malloc(sizeof(connection_payload_t));
        payload->ctx = &ctx;
        payload->client_fd = client_fd;
        pthread_t thread;
        pthread_create(&thread, NULL, connection_thread, payload);
        pthread_detach(thread);
    }
    close(server_fd);
    return 0;
}
