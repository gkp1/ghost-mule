#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include "ProxyBridge.h"

// --- Global UI Widgets ---
static GtkWidget *window;
static GtkTextView *conn_view;
static GtkTextBuffer *conn_buffer;
static GtkTextView *log_view;
static GtkTextBuffer *log_buffer;
static GtkWidget *status_bar;
static guint status_context_id;

// --- Data Structures for thread-safe UI updates ---
typedef struct {
    char *process_name;
    uint32_t pid;
    char *dest_ip;
    uint16_t dest_port;
    char *proxy_info;
    char *timestamp;
} ConnectionData;

typedef struct {
    char *message;
} LogData;

typedef struct {
    GtkWidget *dialog;
    GtkWidget *ip_entry;
    GtkWidget *port_entry;
    GtkWidget *type_combo;
    GtkWidget *user_entry;
    GtkWidget *pass_entry;
    GtkWidget *test_host;
    GtkWidget *test_port;
    GtkTextBuffer *output_buffer;
    GtkWidget *test_btn;
} ConfigInfo;

// --- Config Globals (Defaults) ---
static char g_proxy_ip[256] = "";
static uint16_t g_proxy_port = 0;
static ProxyType g_proxy_type = PROXY_TYPE_SOCKS5;
static char g_proxy_user[256] = "";
static char g_proxy_pass[256] = "";

// --- Helper Functions ---

static char* get_current_time_str() {
    time_t rawtime;
    struct tm *timeinfo;
    char *buffer = malloc(32);
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, 32, "[%H:%M:%S]", timeinfo);
    return buffer;
}

static void free_connection_data(ConnectionData *data) {
    if (data) {
        free(data->process_name);
        free(data->dest_ip);
        free(data->proxy_info);
        free(data->timestamp);
        free(data);
    }
}

// --- GTK Callbacks (Main Thread) ---

// Filter Function for TextView
static void filter_text_view(GtkTextBuffer *buffer, const char *text) {
    if (!buffer) return;
    
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gtk_text_buffer_remove_tag_by_name(buffer, "hidden", &start, &end);

    if (!text || strlen(text) == 0) return;

    GtkTextIter line_start = start;
    while (!gtk_text_iter_is_end(&line_start)) {
        GtkTextIter line_end = line_start;
        if (!gtk_text_iter_ends_line(&line_end))
            gtk_text_iter_forward_to_line_end(&line_end);
        
        char *line_text = gtk_text_buffer_get_text(buffer, &line_start, &line_end, FALSE);
        
        // Case-insensitive search
        char *lower_line = g_utf8_strdown(line_text, -1);
        char *lower_search = g_utf8_strdown(text, -1);

        if (!strstr(lower_line, lower_search)) { 
             GtkTextIter next_line = line_end; 
             gtk_text_iter_forward_char(&next_line); // include newline
             gtk_text_buffer_apply_tag_by_name(buffer, "hidden", &line_start, &next_line);
        }
        
        g_free(lower_line);
        g_free(lower_search);
        g_free(line_text);
        
        gtk_text_iter_forward_line(&line_start);
    }
}

static void on_search_conn_changed(GtkSearchEntry *entry, gpointer user_data) {
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
    filter_text_view(conn_buffer, text);
}

static void on_search_log_changed(GtkSearchEntry *entry, gpointer user_data) {
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
    filter_text_view(log_buffer, text);
}

static void on_clear_conn_clicked(GtkButton *button, gpointer user_data) {
    if (conn_buffer) gtk_text_buffer_set_text(conn_buffer, "", 0);
}

static void on_clear_log_clicked(GtkButton *button, gpointer user_data) {
    if (log_buffer) gtk_text_buffer_set_text(log_buffer, "", 0);
}

// Thread-safe idle callback for Log
static gboolean update_log_gui(gpointer user_data) {
    LogData *data = (LogData *)user_data;
    if (!data) return FALSE;

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(log_buffer, &end);
    
    char full_msg[1200];
    snprintf(full_msg, sizeof(full_msg), "%s %s\n", get_current_time_str(), data->message);
    gtk_text_buffer_insert(log_buffer, &end, full_msg, -1);

    // Limit to 100 lines to prevent memory growth
    while (gtk_text_buffer_get_line_count(log_buffer) > 100) {
        GtkTextIter start, next;
        gtk_text_buffer_get_start_iter(log_buffer, &start);
        next = start;
        gtk_text_iter_forward_line(&next);
        gtk_text_buffer_delete(log_buffer, &start, &next);
    }

    free(data->message);
    free(data);
    return FALSE; // Remove source
}

// Thread-safe idle callback for Connection
static gboolean update_connection_gui_append(gpointer user_data) {
    ConnectionData *data = (ConnectionData *)user_data;
    if (!data) return FALSE;

    if (conn_buffer) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(conn_buffer, &end);
        
        char line_buffer[1024];
        // Format: [Time] Process (PID) -> Target via ProxyInfo
        snprintf(line_buffer, sizeof(line_buffer), "%s %s (PID:%u) -> %s:%u via %s\n", 
                 data->timestamp, data->process_name, data->pid, data->dest_ip, data->dest_port, data->proxy_info);
        
        gtk_text_buffer_insert(conn_buffer, &end, line_buffer, -1);

        // Limit to 100 lines to prevent memory growth
        while (gtk_text_buffer_get_line_count(conn_buffer) > 100) {
            GtkTextIter start, next;
            gtk_text_buffer_get_start_iter(conn_buffer, &start);
            next = start;
            gtk_text_iter_forward_line(&next);
            gtk_text_buffer_delete(conn_buffer, &start, &next);
        }
    }

    free_connection_data(data);
    return FALSE;
}


// --- Library Callbacks ---

static void lib_log_callback(const char *message) {
    LogData *data = malloc(sizeof(LogData));
    data->message = strdup(message);
    g_idle_add(update_log_gui, data);
}

static void lib_connection_callback(const char *process_name, uint32_t pid, const char *dest_ip, uint16_t dest_port, const char *proxy_info) {
    ConnectionData *data = malloc(sizeof(ConnectionData));
    data->process_name = strdup(process_name);
    data->pid = pid;
    data->dest_ip = strdup(dest_ip);
    data->dest_port = dest_port;
    data->proxy_info = strdup(proxy_info);
    data->timestamp = get_current_time_str();
    g_idle_add(update_connection_gui_append, data);
}

// --- Settings Dialog ---

struct TestRunnerData {
    char *host;
    uint16_t port;
    ConfigInfo *ui_info;
};

// Better thread communication
typedef struct {
    char *result_text;
    GtkTextBuffer *buffer;
    GtkWidget *btn;
} TestResultData;

static gboolean on_test_done(gpointer user_data) {
    TestResultData *data = (TestResultData *)user_data;
    gtk_text_buffer_set_text(data->buffer, data->result_text, -1);
    gtk_widget_set_sensitive(data->btn, TRUE);
    
    free(data->result_text);
    free(data);
    return FALSE;
}

static gpointer run_test_thread(gpointer user_data) {
    struct TestRunnerData *req = (struct TestRunnerData *)user_data;
    
    char *buffer = malloc(4096);
    memset(buffer, 0, 4096);
    
    // Check callback if needed, but TestConnection usually returns result in buffer
    ProxyBridge_TestConnection(req->host, req->port, buffer, 4096);
    
    TestResultData *res = malloc(sizeof(TestResultData));
    res->result_text = buffer;
    res->buffer = req->ui_info->output_buffer;
    res->btn = req->ui_info->test_btn;
    
    g_idle_add(on_test_done, res);
    
    free(req->host);
    free(req);
    return NULL;
}

static void on_start_test_clicked(GtkWidget *widget, gpointer data) {
    ConfigInfo *info = (ConfigInfo *)data;
    
    // 1. Validate Proxy Config
    const char *ip_text = gtk_entry_get_text(GTK_ENTRY(info->ip_entry));
    const char *port_text = gtk_entry_get_text(GTK_ENTRY(info->port_entry));
    
    if (!ip_text || strlen(ip_text) == 0 || strspn(port_text, "0123456789") != strlen(port_text) || strlen(port_text) == 0) {
        gtk_text_buffer_set_text(info->output_buffer, "Error: Invalid Proxy IP or Port.", -1);
        return;
    }
    
    // 2. Set Proxy Config
    ProxyType type = (gtk_combo_box_get_active(GTK_COMBO_BOX(info->type_combo)) == 0) ? PROXY_TYPE_HTTP : PROXY_TYPE_SOCKS5;
    int port = atoi(port_text);
    const char *user = gtk_entry_get_text(GTK_ENTRY(info->user_entry));
    const char *pass = gtk_entry_get_text(GTK_ENTRY(info->pass_entry));
    
    ProxyBridge_SetProxyConfig(type, ip_text, port, user, pass);
    
    // 3. Get Test Target
    const char *t_host = gtk_entry_get_text(GTK_ENTRY(info->test_host));
    const char *t_port_s = gtk_entry_get_text(GTK_ENTRY(info->test_port));
    
    if (!t_host || strlen(t_host) == 0) t_host = "google.com";
    int t_port = atoi(t_port_s);
    if (t_port <= 0) t_port = 80;
    
    // 4. Update UI
    gtk_text_buffer_set_text(info->output_buffer, "Testing connection... Please wait...", -1);
    gtk_widget_set_sensitive(info->test_btn, FALSE);
    
    // 5. Run Thread
    struct TestRunnerData *req = malloc(sizeof(struct TestRunnerData));
    req->host = strdup(t_host);
    req->port = t_port;
    req->ui_info = info;
    
    GThread *thread = g_thread_new("test_conn", run_test_thread, req);
    g_thread_unref(thread);
}

static void on_proxy_configure(GtkWidget *widget, gpointer data) {
    ConfigInfo info; // We will pass address to signals, careful about scope if dialog is non-modal? No, dialog_run is blocking.
    // Wait, on_start_test_clicked will be called while dialog_run is blocking main loop? Yes.
    // But info needs to persist. Stack allocation is fine because we don't leave this function until dialog closes.

    GtkWidget *content_area;
    GtkWidget *grid;
    // Removed local declarations that are now in struct ConfigInfo

    info.dialog = gtk_dialog_new_with_buttons("Proxy Settings",
                                         GTK_WINDOW(window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "Cancel", GTK_RESPONSE_CANCEL,
                                         "Save", GTK_RESPONSE_ACCEPT,
                                         NULL);
    // Increase width for log
    gtk_window_set_default_size(GTK_WINDOW(info.dialog), 600, 500);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(info.dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_box_pack_start(GTK_BOX(content_area), grid, TRUE, TRUE, 0);

    // Type
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Type:"), 0, 0, 1, 1);
    info.type_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(info.type_combo), "HTTP");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(info.type_combo), "SOCKS5");
    gtk_combo_box_set_active(GTK_COMBO_BOX(info.type_combo), g_proxy_type == PROXY_TYPE_HTTP ? 0 : 1);
    gtk_grid_attach(GTK_GRID(grid), info.type_combo, 1, 0, 3, 1); // Span 3

    // IP
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Host:"), 0, 1, 1, 1);
    info.ip_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(info.ip_entry), g_proxy_ip);
    gtk_widget_set_hexpand(info.ip_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), info.ip_entry, 1, 1, 3, 1);

    // Port
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Port:"), 0, 2, 1, 1);
    info.port_entry = gtk_entry_new();
    if (g_proxy_port != 0) {
        char port_str[8];
        sprintf(port_str, "%d", g_proxy_port);
        gtk_entry_set_text(GTK_ENTRY(info.port_entry), port_str);
    }
    gtk_grid_attach(GTK_GRID(grid), info.port_entry, 1, 2, 3, 1);

    // User
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Username:"), 0, 3, 1, 1);
    info.user_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(info.user_entry), g_proxy_user);
    gtk_grid_attach(GTK_GRID(grid), info.user_entry, 1, 3, 3, 1);

    // Pass
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Password:"), 0, 4, 1, 1);
    info.pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(info.pass_entry), FALSE);
    gtk_entry_set_text(GTK_ENTRY(info.pass_entry), g_proxy_pass);
    gtk_grid_attach(GTK_GRID(grid), info.pass_entry, 1, 4, 3, 1);

    // --- Test Section ---
    gtk_grid_attach(GTK_GRID(grid), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, 5, 4, 1);
    
    GtkWidget *test_label = gtk_label_new("<b>Test Connection</b>");
    gtk_label_set_use_markup(GTK_LABEL(test_label), TRUE);
    gtk_widget_set_halign(test_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), test_label, 0, 6, 4, 1);

    // Target Host
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Target:"), 0, 7, 1, 1);
    info.test_host = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(info.test_host), "google.com");
    gtk_grid_attach(GTK_GRID(grid), info.test_host, 1, 7, 1, 1);
    
    // Target Port
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Port:"), 2, 7, 1, 1);
    info.test_port = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(info.test_port), "80");
    gtk_widget_set_size_request(info.test_port, 80, -1);
    gtk_grid_attach(GTK_GRID(grid), info.test_port, 3, 7, 1, 1);

    // Start Test Button
    info.test_btn = gtk_button_new_with_label("Start Test");
    g_signal_connect(info.test_btn, "clicked", G_CALLBACK(on_start_test_clicked), &info);
    gtk_grid_attach(GTK_GRID(grid), info.test_btn, 0, 8, 4, 1);

    // Output Log
    GtkWidget *out_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(out_scroll, -1, 150);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(out_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    
    GtkWidget *out_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(out_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(out_view), TRUE);
    info.output_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(out_view));
    
    gtk_container_add(GTK_CONTAINER(out_scroll), out_view);
    gtk_grid_attach(GTK_GRID(grid), out_scroll, 0, 9, 4, 1);


    gtk_widget_show_all(info.dialog);

    while (TRUE) {
        if (gtk_dialog_run(GTK_DIALOG(info.dialog)) != GTK_RESPONSE_ACCEPT) break;

        // Validation
        const char *ip_text = gtk_entry_get_text(GTK_ENTRY(info.ip_entry));
        const char *port_text = gtk_entry_get_text(GTK_ENTRY(info.port_entry));

        if (!ip_text || strlen(ip_text) == 0) {
            GtkWidget *err = gtk_message_dialog_new(GTK_WINDOW(info.dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Host (IP/Domain) cannot be empty.");
            gtk_dialog_run(GTK_DIALOG(err));
            gtk_widget_destroy(err);
            continue;
        }

        if (strspn(port_text, "0123456789") != strlen(port_text) || strlen(port_text) == 0) {
            GtkWidget *err = gtk_message_dialog_new(GTK_WINDOW(info.dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Port must be a valid number.");
            gtk_dialog_run(GTK_DIALOG(err));
            gtk_widget_destroy(err);
            continue;
        }

        int p = atoi(port_text);
        if (p < 1 || p > 65535) {
            GtkWidget *err = gtk_message_dialog_new(GTK_WINDOW(info.dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Port must be between 1 and 65535.");
             gtk_dialog_run(GTK_DIALOG(err));
             gtk_widget_destroy(err);
             continue;
        }

        // Save
        g_proxy_type = (gtk_combo_box_get_active(GTK_COMBO_BOX(info.type_combo)) == 0) ? PROXY_TYPE_HTTP : PROXY_TYPE_SOCKS5;
        strncpy(g_proxy_ip, ip_text, sizeof(g_proxy_ip)-1);
        g_proxy_port = p;
        strncpy(g_proxy_user, gtk_entry_get_text(GTK_ENTRY(info.user_entry)), sizeof(g_proxy_user)-1);
        strncpy(g_proxy_pass, gtk_entry_get_text(GTK_ENTRY(info.pass_entry)), sizeof(g_proxy_pass)-1);

        ProxyBridge_SetProxyConfig(g_proxy_type, g_proxy_ip, g_proxy_port, g_proxy_user, g_proxy_pass);
        
        char status_msg[512];
        snprintf(status_msg, sizeof(status_msg), "Configuration updated: %s:%d", g_proxy_ip, g_proxy_port);
        gtk_statusbar_push(GTK_STATUSBAR(status_bar), status_context_id, status_msg);
        break;
    }

    gtk_widget_destroy(info.dialog);
}

static void on_about(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                           GTK_DIALOG_DESTROY_WITH_PARENT,
                                           GTK_MESSAGE_INFO,
                                           GTK_BUTTONS_OK,
                                           "ProxyBridge Linux GUI\nVersion 4.0-Beta\n\nHigh-performance zero-copy proxy client.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void signal_handler(int sig) {
    fprintf(stderr, "\nSignal %d received. Stopping ProxyBridge...\n", sig);
    ProxyBridge_Stop();
    exit(sig);
}

static void on_window_destroy(GtkWidget *widget, gpointer data) {
    ProxyBridge_Stop();
    gtk_main_quit();
}

// --- Main Init ---

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGSEGV, signal_handler);

    // Only verify root if not argument --help or similar passed? 
    // But GTK also takes args. 
    // Simply check uid.
    if (getuid() != 0) {
        // Can't show GUI dialog easily without GTK init which might fail if no display.
        // Try init, then dialog.
        gtk_init(&argc, &argv); 
        GtkWidget *dialog = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "ProxyBridge must be run as root (sudo).");
        gtk_dialog_run(GTK_DIALOG(dialog));
        return 1;
    }

    gtk_init(&argc, &argv);

    // Apply dark theme preference if available
    GtkSettings *settings = gtk_settings_get_default();
    if (settings) {
        g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);
    }

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "ProxyBridge");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // Menu Bar
    GtkWidget *menubar = gtk_menu_bar_new();
    
    GtkWidget *proxy_menu_item = gtk_menu_item_new_with_label("Proxy");
    GtkWidget *proxy_menu = gtk_menu_new();
    GtkWidget *config_item = gtk_menu_item_new_with_label("Proxy Settings");
    GtkWidget *exit_item = gtk_menu_item_new_with_label("Exit");
    g_signal_connect(config_item, "activate", G_CALLBACK(on_proxy_configure), NULL);
    g_signal_connect(exit_item, "activate", G_CALLBACK(on_window_destroy), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(proxy_menu), config_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(proxy_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(proxy_menu), exit_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(proxy_menu_item), proxy_menu);
    
    GtkWidget *about_menu_item = gtk_menu_item_new_with_label("About");
    g_signal_connect(about_menu_item, "activate", G_CALLBACK(on_about), NULL);

    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), proxy_menu_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), about_menu_item);
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);

    // Tabs
    GtkWidget *notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

    // --- Tab 1: Connections ---
    GtkWidget *conn_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(conn_vbox), 5);

    // Toolbar (Search + Clear)
    GtkWidget *conn_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    GtkWidget *conn_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(conn_search), "Search connections...");
    g_signal_connect(conn_search, "search-changed", G_CALLBACK(on_search_conn_changed), NULL);

    GtkWidget *conn_clear_btn = gtk_button_new_with_label("Clear Logs");
    g_signal_connect(conn_clear_btn, "clicked", G_CALLBACK(on_clear_conn_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(conn_toolbar), conn_search, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(conn_toolbar), conn_clear_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(conn_vbox), conn_toolbar, FALSE, FALSE, 0);

    // List View (Now Text View)
    conn_view = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(conn_view, FALSE);
    gtk_text_view_set_cursor_visible(conn_view, FALSE);
    conn_buffer = gtk_text_view_get_buffer(conn_view);
    
    // Create tag for filtering
    gtk_text_buffer_create_tag(conn_buffer, "hidden", "invisible", TRUE, NULL);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled_window), GTK_WIDGET(conn_view));
    gtk_box_pack_start(GTK_BOX(conn_vbox), scrolled_window, TRUE, TRUE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), conn_vbox, gtk_label_new("Connections"));

    // --- Tab 2: Activity Logs ---
    GtkWidget *log_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(log_vbox), 5);
    
    // Toolbar (Search + Clear)
    GtkWidget *log_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    GtkWidget *log_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(log_search), "Search logs...");
    g_signal_connect(log_search, "search-changed", G_CALLBACK(on_search_log_changed), NULL);

    GtkWidget *log_clear_btn = gtk_button_new_with_label("Clear Logs");
    g_signal_connect(log_clear_btn, "clicked", G_CALLBACK(on_clear_log_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(log_toolbar), log_search, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(log_toolbar), log_clear_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(log_vbox), log_toolbar, FALSE, FALSE, 0);

    log_view = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(log_view, FALSE);
    gtk_text_view_set_cursor_visible(log_view, FALSE);
    log_buffer = gtk_text_view_get_buffer(log_view);

    // Create tag for filtering
    gtk_text_buffer_create_tag(log_buffer, "hidden", "invisible", TRUE, NULL);
    
    GtkWidget *log_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(log_scroll), GTK_WIDGET(log_view));
    gtk_box_pack_start(GTK_BOX(log_vbox), log_scroll, TRUE, TRUE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), log_vbox, gtk_label_new("Activity Logs"));

    // Status Bar
    status_bar = gtk_statusbar_new();
    status_context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(status_bar), "Status");
    gtk_box_pack_start(GTK_BOX(vbox), status_bar, FALSE, FALSE, 0);

    // Initial Config
    ProxyBridge_SetLogCallback(lib_log_callback);
    ProxyBridge_SetConnectionCallback(lib_connection_callback);
    ProxyBridge_SetTrafficLoggingEnabled(true);
    
    // Start Proxy Engine
    if (ProxyBridge_Start()) {
        gtk_statusbar_push(GTK_STATUSBAR(status_bar), status_context_id, "ProxyBridge Service Started. Please configure proxy settings.");
    } else {
        gtk_statusbar_push(GTK_STATUSBAR(status_bar), status_context_id, "Failed to start ProxyBridge engine.");
    }

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
