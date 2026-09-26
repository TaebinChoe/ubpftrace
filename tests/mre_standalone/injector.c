#include <stdio.h>
#include <stdlib.h>
#include <frida-core.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <PID> <path_to_so> [entry_function]\n", argv[0]);
        return 1;
    }
    int pid = atoi(argv[1]);
    const char *so_path = argv[2];
    const char *entry = (argc >= 4) ? argv[3] : "mre_agent_init";

    printf("Injecting %s into PID %d (entry: %s)...\n", so_path, pid, entry);
    frida_init();
    FridaInjector *injector = frida_injector_new();
    GError *err = NULL;
    unsigned int id = frida_injector_inject_library_file_sync(
        injector, pid, so_path, entry, "", NULL, &err
    );
    if (err) {
        fprintf(stderr, "Failed to inject: %s\n", err->message);
        g_error_free(err);
        frida_unref(injector);
        frida_deinit();
        return 1;
    }
    printf("Successfully injected! ID: %u\n", id);
    frida_injector_close_sync(injector, NULL, NULL);
    frida_unref(injector);
    frida_deinit();
    return 0;
}
