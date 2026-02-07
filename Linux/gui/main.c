#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <gtk/gtk.h>
#include <unistd.h>
#include "proxybridge.h"
#include "main_window.h"

// Library handle
static void *lib_handle = NULL;

// Function pointers
uint32_t (*ProxyBridge_AddRule)(const char *, const char *, const char *, RuleProtocol, RuleAction) = NULL;
bool (*ProxyBridge_EnableRule)(uint32_t) = NULL;
bool (*ProxyBridge_DisableRule)(uint32_t) = NULL;
bool (*ProxyBridge_DeleteRule)(uint32_t) = NULL;
bool (*ProxyBridge_EditRule)(uint32_t, const char *, const char *, const char *, RuleProtocol, RuleAction) = NULL;
bool (*ProxyBridge_SetProxyConfig)(ProxyType, const char *, uint16_t, const char *, const char *) = NULL;
void (*ProxyBridge_SetLogCallback)(LogCallback) = NULL;
void (*ProxyBridge_SetConnectionCallback)(ConnectionCallback) = NULL;
void (*ProxyBridge_SetTrafficLoggingEnabled)(bool) = NULL;
void (*ProxyBridge_SetDnsViaProxy)(bool) = NULL;
bool (*ProxyBridge_Start)(void) = NULL;
bool (*ProxyBridge_Stop)(void) = NULL;

// Callback handlers
static void log_callback(const char *message) {
    add_activity_log(message);
}

static void connection_callback(const char *processName, uint32_t pid,
                                const char *destIp, uint16_t destPort,
                                const char *proxyInfo) {
    char msg[512];
    snprintf(msg, sizeof(msg), "%s (PID %u) → %s:%u via %s",
             processName, pid, destIp, destPort, proxyInfo);
    add_connection_log(msg);
}

bool load_proxybridge_library(void)
{
    lib_handle = dlopen("libproxybridge.so", RTLD_LAZY);
    if (!lib_handle) {
        lib_handle = dlopen("/usr/local/lib/libproxybridge.so", RTLD_LAZY);
    }
    
    if (!lib_handle) {
        fprintf(stderr, "Failed to load libproxybridge.so: %s\n", dlerror());
        return false;
    }
    
    #define LOAD_SYMBOL(name) \
        name = dlsym(lib_handle, #name); \
        if (!name) { \
            fprintf(stderr, "Failed to load symbol %s: %s\n", #name, dlerror()); \
            dlclose(lib_handle); \
            return false; \
        }
    
    LOAD_SYMBOL(ProxyBridge_AddRule);
    LOAD_SYMBOL(ProxyBridge_EnableRule);
    LOAD_SYMBOL(ProxyBridge_DisableRule);
    LOAD_SYMBOL(ProxyBridge_DeleteRule);
    LOAD_SYMBOL(ProxyBridge_EditRule);
    LOAD_SYMBOL(ProxyBridge_SetProxyConfig);
    LOAD_SYMBOL(ProxyBridge_SetLogCallback);
    LOAD_SYMBOL(ProxyBridge_SetConnectionCallback);
    LOAD_SYMBOL(ProxyBridge_SetTrafficLoggingEnabled);
    LOAD_SYMBOL(ProxyBridge_SetDnsViaProxy);
    LOAD_SYMBOL(ProxyBridge_Start);
    LOAD_SYMBOL(ProxyBridge_Stop);
    
    #undef LOAD_SYMBOL
    
    // Set up callbacks
    ProxyBridge_SetLogCallback(log_callback);
    ProxyBridge_SetConnectionCallback(connection_callback);
    
    return true;
}

void unload_proxybridge_library(void)
{
    if (lib_handle) {
        // Stop proxy before unloading (critical cleanup)
        if (ProxyBridge_Stop) {
            ProxyBridge_Stop();
        }
        dlclose(lib_handle);
        lib_handle = NULL;
    }
}

static gboolean check_root_privileges(void)
{
    if (geteuid() != 0) {
        GtkWidget *dialog = gtk_message_dialog_new(
            NULL,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "ProxyBridge requires root privileges"
        );
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog),
            "Please run with: sudo ./ProxyBridge-GUI"
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return FALSE;
    }
    return TRUE;
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);
    
    // Check root privileges
    if (!check_root_privileges()) {
        return 1;
    }
    
    // Load ProxyBridge library
    if (!load_proxybridge_library()) {
        GtkWidget *dialog = gtk_message_dialog_new(
            NULL,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "Failed to load ProxyBridge library"
        );
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog),
            "Make sure libproxybridge.so is in /usr/local/lib/"
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return 1;
    }
    
    // Create main window
    create_main_window();
    
    // Run GTK main loop
    gtk_main();
    
    // Cleanup
    unload_proxybridge_library();
    
    return 0;
}
