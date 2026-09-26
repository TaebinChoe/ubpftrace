#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dlfcn.h>
#include <frida-gum.h>

static atomic_uint_fast64_t g_hit_count = 0;
static GumInterceptor *g_interceptor = NULL;
static GumInvocationListener *g_listener = NULL;
static void *g_target_func = NULL;
static pthread_mutex_t g_hook_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int g_initialized = 0;

static void on_enter(GumInvocationContext *context, gpointer user_data) {
    (void)context;
    (void)user_data;
    atomic_fetch_add_explicit(&g_hit_count, 1, memory_order_relaxed);
}

static void on_leave(GumInvocationContext *context, gpointer user_data) {
    (void)context;
    (void)user_data;
}

// Helper to find function address in main executable
static void *find_target_function() {
    // 1. Try RTLD_DEFAULT dlsym
    void *addr = dlsym(RTLD_DEFAULT, "target_work");
    if (addr) return addr;

    // 2. Try export lookup via Gum
    addr = (void *)gum_module_find_export_by_name(NULL, "target_work");
    if (addr) return addr;

    // 3. Try dlopen(NULL)
    void *h = dlopen(NULL, RTLD_LAZY);
    if (h) {
        addr = dlsym(h, "target_work");
        dlclose(h);
        if (addr) return addr;
    }
    return NULL;
}

static int do_attach() {
    pthread_mutex_lock(&g_hook_lock);
    if (g_listener != NULL) {
        pthread_mutex_unlock(&g_hook_lock);
        return 0; // Already attached
    }
    if (!g_target_func) {
        g_target_func = find_target_function();
        if (!g_target_func) {
            fprintf(stderr, "[AGENT] ERROR: Could not find target_work function address!\n");
            pthread_mutex_unlock(&g_hook_lock);
            return -1;
        }
    }

    g_listener = gum_make_call_listener(on_enter, on_leave, NULL, NULL);
    gum_interceptor_begin_transaction(g_interceptor);
    GumAttachReturn ret = gum_interceptor_attach(g_interceptor, g_target_func, g_listener, NULL);
    gum_interceptor_end_transaction(g_interceptor);

    fprintf(stderr, "[AGENT] Attached hook to target_work at %p (result=%d)\n", g_target_func, (int)ret);
    pthread_mutex_unlock(&g_hook_lock);
    return (ret == GUM_ATTACH_OK) ? 0 : -1;
}

static int do_detach() {
    pthread_mutex_lock(&g_hook_lock);
    if (g_listener == NULL) {
        pthread_mutex_unlock(&g_hook_lock);
        return 0; // Already detached
    }

    gum_interceptor_begin_transaction(g_interceptor);
    gum_interceptor_detach(g_interceptor, g_listener);
    gum_interceptor_end_transaction(g_interceptor);

    g_object_unref(g_listener);
    g_listener = NULL;

    fprintf(stderr, "[AGENT] Detached hook cleanly from target_work at %p\n", g_target_func);
    pthread_mutex_unlock(&g_hook_lock);
    return 0;
}

static void *ipc_thread_func(void *arg) {
    (void)arg;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    char name[64];
    snprintf(name, sizeof(name), "mre-agent-%d", getpid());
    addr.sun_path[0] = '\0';
    memcpy(addr.sun_path + 1, name, strlen(name));
    socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(name);

    if (bind(fd, (struct sockaddr *)&addr, len) != 0 || listen(fd, 5) != 0) {
        close(fd);
        return NULL;
    }

    fprintf(stderr, "[AGENT] IPC server listening on abstract socket: %s\n", name);

    while (1) {
        int cfd = accept(fd, NULL, NULL);
        if (cfd < 0) continue;

        char buf[256] = {0};
        ssize_t n = read(cfd, buf, sizeof(buf) - 1);
        if (n > 0) {
            // Trim newline
            while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) buf[--n] = '\0';

            if (strcmp(buf, "status") == 0) {
                char resp[128];
                int is_attached = (g_listener != NULL);
                uint64_t count = atomic_load(&g_hit_count);
                snprintf(resp, sizeof(resp), "count=%lu attached=%d\n", count, is_attached);
                write(cfd, resp, strlen(resp));
            } else if (strcmp(buf, "detach") == 0) {
                int res = do_detach();
                if (res == 0) write(cfd, "ok\n", 3);
                else write(cfd, "err\n", 4);
            } else if (strcmp(buf, "reattach") == 0) {
                int res = do_attach();
                if (res == 0) write(cfd, "ok\n", 3);
                else write(cfd, "err\n", 4);
            } else {
                write(cfd, "unknown\n", 8);
            }
        }
        close(cfd);
    }
    return NULL;
}

void mre_agent_init(const char *data, int *stay_resident) {
    (void)data;
    if (stay_resident) {
        *stay_resident = 1;
    }
    int expected = 0;
    if (!atomic_compare_exchange_strong(&g_initialized, &expected, 1)) {
        fprintf(stderr, "[AGENT] Already initialized, ignoring re-init\n");
        return;
    }

    fprintf(stderr, "[AGENT] mre_agent_init running in PID %d\n", getpid());
    gum_init_embedded();
    g_interceptor = gum_interceptor_obtain();

    // Initial attach
    do_attach();

    // Start background IPC control thread
    pthread_t th;
    pthread_create(&th, NULL, ipc_thread_func, NULL);
    pthread_detach(th);
}
