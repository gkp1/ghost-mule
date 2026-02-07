#include <unistd.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "ProxyBridge.h"

static long safe_strtol(const char *nptr) {
    if (!nptr) return 0; // Handle NULL
    char *endptr;
    long val = strtol(nptr, &endptr, 10);
    // You could check errno here if needed, but for GUI fields 0 is often a safe fallback or filtered before
    if (endptr == nptr) return 0; // No digits found
    return val;
}

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

typedef struct {
    uint32_t id;
    char *process_name;
    char *target_hosts;
    char *target_ports;
    RuleProtocol protocol;
    RuleAction action;
    bool enabled;
    bool selected;
} RuleData;

static GList *g_rules_list = NULL;
static GtkWidget *rules_list_box = NULL;

// --- Helper Functions ---
// Forward declaration
static void refresh_rules_ui();

static void free_rule_data(RuleData *rule) {
    if (rule) {
        if (rule->process_name) free(rule->process_name);
        if (rule->target_hosts) free(rule->target_hosts);
        if (rule->target_ports) free(rule->target_ports);
        free(rule);
    }
}

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
    int port = (int)safe_strtol(port_text);
    const char *user = gtk_entry_get_text(GTK_ENTRY(info->user_entry));
    const char *pass = gtk_entry_get_text(GTK_ENTRY(info->pass_entry));
    
    ProxyBridge_SetProxyConfig(type, ip_text, port, user, pass);
    
    // 3. Get Test Target
    const char *t_host = gtk_entry_get_text(GTK_ENTRY(info->test_host));
    const char *t_port_s = gtk_entry_get_text(GTK_ENTRY(info->test_port));
    
    if (!t_host || strlen(t_host) == 0) t_host = "google.com";
    int t_port = (int)safe_strtol(t_port_s);
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
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", g_proxy_port);
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

        int p = (int)safe_strtol(port_text);
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

// --- Rules Management ---

static void on_rule_delete(GtkWidget *widget, gpointer data) {
    RuleData *rule = (RuleData *)data;
    
    // Call C API
    ProxyBridge_DeleteRule(rule->id);
    
    // Remove from list
    g_rules_list = g_list_remove(g_rules_list, rule);
    free_rule_data(rule);
    
    // UI Refresh
    refresh_rules_ui();
}

static void on_rule_toggle(GtkToggleButton *btn, gpointer data) {
    RuleData *rule = (RuleData *)data;
    rule->enabled = gtk_toggle_button_get_active(btn);
    
    if (rule->enabled) {
        ProxyBridge_EnableRule(rule->id);
    } else {
        ProxyBridge_DisableRule(rule->id);
    }
}

static void on_save_rule(GtkWidget *widget, gpointer data) {
    GtkWidget **widgets = (GtkWidget **)data;
    GtkWidget *dialog = widgets[0];
    RuleData *edit_rule = (RuleData *)widgets[6]; // If not null, we are editing
    
    const char *proc = gtk_entry_get_text(GTK_ENTRY(widgets[1]));
    const char *hosts = gtk_entry_get_text(GTK_ENTRY(widgets[2]));
    const char *ports = gtk_entry_get_text(GTK_ENTRY(widgets[3]));
    RuleProtocol proto = gtk_combo_box_get_active(GTK_COMBO_BOX(widgets[4]));
    RuleAction action = gtk_combo_box_get_active(GTK_COMBO_BOX(widgets[5]));

    if (strlen(proc) == 0) {
        // Error
        return;
    }
    
    if (edit_rule) {
        // Edit Existing
        ProxyBridge_EditRule(edit_rule->id, proc, hosts, ports, proto, action);
        
        // Update Local
        free(edit_rule->process_name); edit_rule->process_name = strdup(proc);
        free(edit_rule->target_hosts); edit_rule->target_hosts = strdup(hosts);
        free(edit_rule->target_ports); edit_rule->target_ports = strdup(ports);
        edit_rule->protocol = proto;
        edit_rule->action = action;
        
    } else {
        // Add New
        uint32_t new_id = ProxyBridge_AddRule(proc, hosts, ports, proto, action);
        
        RuleData *new_rule = malloc(sizeof(RuleData));
        new_rule->id = new_id;
        new_rule->process_name = strdup(proc);
        new_rule->target_hosts = strdup(hosts);
        new_rule->target_ports = strdup(ports);
        new_rule->protocol = proto;
        new_rule->action = action;
        new_rule->enabled = true; // Default enabled
        new_rule->selected = false;
        
        g_rules_list = g_list_append(g_rules_list, new_rule);
    }
    
    refresh_rules_ui();
    gtk_widget_destroy(dialog);
    free(widgets);
}

static void on_browse_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *entry = (GtkWidget *)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Application",
                                      NULL,
                                      GTK_FILE_CHOOSER_ACTION_OPEN,
                                      "_Cancel", GTK_RESPONSE_CANCEL,
                                      "_Select", GTK_RESPONSE_ACCEPT,
                                      NULL);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        char *base = g_path_get_basename(filename);
        gtk_entry_set_text(GTK_ENTRY(entry), base);
        g_free(base);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void open_rule_dialog(RuleData *rule) {
    GtkWidget *dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), rule ? "Edit Rule" : "Add Rule");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);
    
    // Process
    GtkWidget *proc_entry = gtk_entry_new();
    GtkWidget *browse_btn = gtk_button_new_with_label("Browse...");
    g_signal_connect(browse_btn, "clicked", G_CALLBACK(on_browse_clicked), proc_entry);
    
    GtkWidget *proc_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(proc_box), proc_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(proc_box), browse_btn, FALSE, FALSE, 0);

    GtkWidget *host_entry = gtk_entry_new();
    GtkWidget *port_entry = gtk_entry_new();
    
    // Protocol
    GtkWidget *proto_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(proto_combo), "TCP");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(proto_combo), "UDP");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(proto_combo), "BOTH");
    
    // Action
    GtkWidget *action_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(action_combo), "PROXY");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(action_combo), "DIRECT");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(action_combo), "BLOCK");

    if (rule) {
        gtk_entry_set_text(GTK_ENTRY(proc_entry), rule->process_name);
        gtk_entry_set_text(GTK_ENTRY(host_entry), rule->target_hosts);
        gtk_entry_set_text(GTK_ENTRY(port_entry), rule->target_ports);
        gtk_combo_box_set_active(GTK_COMBO_BOX(proto_combo), rule->protocol);
        gtk_combo_box_set_active(GTK_COMBO_BOX(action_combo), rule->action);
    } else {
        gtk_entry_set_text(GTK_ENTRY(port_entry), "*"); // Default
        gtk_entry_set_text(GTK_ENTRY(host_entry), "*"); // Default
        gtk_combo_box_set_active(GTK_COMBO_BOX(proto_combo), 2); // BOTH
        gtk_combo_box_set_active(GTK_COMBO_BOX(action_combo), 0); // PROXY
    }

    int row = 0;
    
    // Process Row
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Process Name:"), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), proc_box, 1, row, 1, 1);
    row++;
    
    // Process Hint
    GtkWidget *proc_hint = gtk_label_new("Example: firefox; chrome; /usr/bin/wget");
    gtk_style_context_add_class(gtk_widget_get_style_context(proc_hint), "dim-label");
    gtk_widget_set_halign(proc_hint, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), proc_hint, 1, row, 1, 1);
    row++;

    // Host Row
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Target Host:"), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), host_entry, 1, row, 1, 1);
    row++;
    
    // Host Hint
    GtkWidget *host_hint = gtk_label_new("Example: 192.168.1.*; 10.0.0.1-50; *");
    gtk_style_context_add_class(gtk_widget_get_style_context(host_hint), "dim-label");
    gtk_widget_set_halign(host_hint, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), host_hint, 1, row, 1, 1);
    row++;

    // Port Row
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Target Port:"), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), port_entry, 1, row, 1, 1);
    row++;
    
    // Port Hint
    GtkWidget *port_hint = gtk_label_new("Example: 80; 443; 8000-8080; *");
    gtk_style_context_add_class(gtk_widget_get_style_context(port_hint), "dim-label");
    gtk_widget_set_halign(port_hint, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), port_hint, 1, row, 1, 1);
    row++;
    
    // Protocol
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Protocol:"), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), proto_combo, 1, row, 1, 1);
    row++;
    
    // Action
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Action:"), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), action_combo, 1, row, 1, 1);
    row++;

    gtk_container_add(GTK_CONTAINER(content), grid);
    
    GtkWidget *save_btn = gtk_button_new_with_label("Save");
    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), cancel_btn, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), save_btn, GTK_RESPONSE_ACCEPT);
    
    // Pass everything needed to callback
    GtkWidget **data = malloc(7 * sizeof(GtkWidget*));
    data[0] = dialog;
    data[1] = proc_entry;
    data[2] = host_entry;
    data[3] = port_entry;
    data[4] = proto_combo;
    data[5] = action_combo;
    data[6] = (GtkWidget*)rule; // Hacky cast
    
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_rule), data);
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(gtk_widget_destroy), NULL); 
    // Simplified for now.

    gtk_widget_show_all(dialog);
}

static void on_rule_edit(GtkWidget *widget, gpointer data) {
    RuleData *rule = (RuleData *)data;
    open_rule_dialog(rule);
}

static void on_rule_add_clicked(GtkWidget *widget, gpointer data) {
    open_rule_dialog(NULL);
}

static GtkWidget *btn_select_all_header = NULL; // Renamed to separate from any local vars

// Note: rules_list_box is already defined at top of file, so we just use it
static void refresh_rules_ui(); // Forward decl

static void on_rule_select_toggle(GtkToggleButton *btn, gpointer data) {
    RuleData *rule = (RuleData *)data;
    rule->selected = gtk_toggle_button_get_active(btn);
    // Refresh only the Select All button label
    if (btn_select_all_header) {
         bool all_selected = (g_rules_list != NULL);
         if (g_rules_list == NULL) all_selected = false;
         for (GList *l = g_rules_list; l != NULL; l = l->next) {
            RuleData *r = (RuleData *)l->data;
            if (!r->selected) {
                all_selected = false;
                break;
            }
        }
        gtk_button_set_label(GTK_BUTTON(btn_select_all_header), all_selected ? "Deselect All" : "Select All");
    }
}

// --- JSON Helpers ---
static char *escape_json_string(const char *src) {
    if (!src) return strdup("");
    GString *str = g_string_new("");
    for (const char *p = src; *p; p++) {
        if (*p == '\\') g_string_append(str, "\\\\");
        else if (*p == '"') g_string_append(str, "\\\"");
        else if (*p == '\n') g_string_append(str, "\\n");
        else g_string_append_c(str, *p);
    }
    return g_string_free(str, FALSE);
}

// Very basic JSON parser for valid input
static char *extract_sub_json_str(const char *json, const char *key) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    char *k = strstr(json, search_key);
    if (!k) return NULL;
    char *colon = strchr(k, ':');
    if (!colon) return NULL;
    char *val_start = strchr(colon, '"');
    if (!val_start) return NULL;
    val_start++;
    char *val_end = strchr(val_start, '"');
    if (!val_end) return NULL;
    return g_strndup(val_start, val_end - val_start);
}

static bool extract_sub_json_bool(const char *json, const char *key) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    char *k = strstr(json, search_key);
    if (!k) return false;
    char *colon = strchr(k, ':');
    if (!colon) return false;
    // Skip spaces
    char *v = colon + 1;
    while(*v == ' ' || *v == '\t') v++;
    if (strncmp(v, "true", 4) == 0) return true;
    return false;
}

static void on_rule_export_clicked(GtkWidget *widget, gpointer data) {
    if (!g_rules_list) return;
    
    // Check if any selected
    bool any_selected = false;
    for (GList *l = g_rules_list; l != NULL; l = l->next) {
        if (((RuleData *)l->data)->selected) { any_selected = true; break; }
    }
    if (!any_selected) {
        GtkWidget *msg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "Please select at least one rule to export.");
        gtk_dialog_run(GTK_DIALOG(msg));
        gtk_widget_destroy(msg);
        return;
    }

    GtkWidget *dialog = gtk_file_chooser_dialog_new("Export Rules",
                                      GTK_WINDOW(window),
                                      GTK_FILE_CHOOSER_ACTION_SAVE,
                                      "_Cancel", GTK_RESPONSE_CANCEL,
                                      "_Save", GTK_RESPONSE_ACCEPT,
                                      NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "proxy_rules.json");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        FILE *f = fopen(filename, "w");
        if (f) {
            fprintf(f, "[\n");
            bool first = true;
            for (GList *l = g_rules_list; l != NULL; l = l->next) {
                RuleData *r = (RuleData *)l->data;
                if (!r->selected) continue;
                
                if (!first) fprintf(f, ",\n");
                char *proc = escape_json_string(r->process_name);
                char *host = escape_json_string(r->target_hosts);
                char *port = escape_json_string(r->target_ports);
                const char *proto = (r->protocol == RULE_PROTOCOL_TCP) ? "TCP" : (r->protocol == RULE_PROTOCOL_UDP ? "UDP" : "BOTH");
                const char *act = (r->action == RULE_ACTION_PROXY) ? "PROXY" : (r->action == RULE_ACTION_DIRECT ? "DIRECT" : "BLOCK");
                
                fprintf(f, "  {\n");
                fprintf(f, "    \"processNames\": \"%s\",\n", proc);
                fprintf(f, "    \"targetHosts\": \"%s\",\n", host);
                fprintf(f, "    \"targetPorts\": \"%s\",\n", port);
                fprintf(f, "    \"protocol\": \"%s\",\n", proto);
                fprintf(f, "    \"action\": \"%s\",\n", act);
                fprintf(f, "    \"enabled\": %s\n", r->enabled ? "true" : "false");
                fprintf(f, "  }");
                
                g_free(proc); g_free(host); g_free(port);
                first = false;
            }
            fprintf(f, "\n]\n");
            fclose(f);
            
            GtkWidget *msg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Rules exported successfully.");
            gtk_dialog_run(GTK_DIALOG(msg));
            gtk_widget_destroy(msg);
        }
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_rule_import_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Import Rules",
                                      GTK_WINDOW(window),
                                      GTK_FILE_CHOOSER_ACTION_OPEN,
                                      "_Cancel", GTK_RESPONSE_CANCEL,
                                      "_Open", GTK_RESPONSE_ACCEPT,
                                      NULL);
                                      
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        char *content = NULL;
        gsize len;
        
        if (g_file_get_contents(filename, &content, &len, NULL)) {
            // Simple robust scan: look for { ... } blocks
            char *curr = content;
            int imported = 0;
            while ((curr = strchr(curr, '{')) != NULL) {
                char *end = strchr(curr, '}');
                if (!end) break;
                
                // Temp terminate to limit search scope
                char saved = *end;
                *end = '\0';
                
                // Parse
                char *proc = extract_sub_json_str(curr, "processNames");
                char *host = extract_sub_json_str(curr, "targetHosts");
                char *port = extract_sub_json_str(curr, "targetPorts");
                char *proto_s = extract_sub_json_str(curr, "protocol");
                char *act_s = extract_sub_json_str(curr, "action");
                bool en = extract_sub_json_bool(curr, "enabled");
                
                if (proc && host && port && proto_s && act_s) {
                    RuleProtocol p = RULE_PROTOCOL_BOTH;
                    if (strcmp(proto_s, "TCP") == 0) p = RULE_PROTOCOL_TCP;
                    else if (strcmp(proto_s, "UDP") == 0) p = RULE_PROTOCOL_UDP;
                    
                    RuleAction a = RULE_ACTION_PROXY;
                    if (strcmp(act_s, "DIRECT") == 0) a = RULE_ACTION_DIRECT;
                    else if (strcmp(act_s, "BLOCK") == 0) a = RULE_ACTION_BLOCK;
                    
                    uint32_t nid = ProxyBridge_AddRule(proc, host, port, p, a);
                    // Update enabled if needed (AddRule creates enabled by default, but check ID)
                     if (!en) ProxyBridge_DisableRule(nid);
                    
                    // Add to UI list struct
                    RuleData *nd = malloc(sizeof(RuleData));
                    nd->id = nid;
                    nd->process_name = strdup(proc);
                    nd->target_hosts = strdup(host);
                    nd->target_ports = strdup(port);
                    nd->protocol = p;
                    nd->action = a;
                    nd->enabled = en;
                    nd->selected = false;
                    
                    g_rules_list = g_list_append(g_rules_list, nd);
                    imported++;
                }
                
                g_free(proc); g_free(host); g_free(port); g_free(proto_s); g_free(act_s);
                
                *end = saved; // Restore
                curr = end + 1;
            }
            g_free(content);
            
            if (imported > 0) {
                 refresh_rules_ui();
                 GtkWidget *msg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Imported %d rules.", imported);
                 gtk_dialog_run(GTK_DIALOG(msg));
                 gtk_widget_destroy(msg);
            }
        }
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_bulk_delete_clicked(GtkWidget *widget, gpointer data) {
    if (!g_rules_list) return;

    GList *iter = g_rules_list;
    // Collect to-delete items first
    GList *to_delete = NULL;
    
    while (iter != NULL) {
        RuleData *rule = (RuleData *)iter->data;
        if (rule->selected) {
            to_delete = g_list_append(to_delete, rule);
        }
        iter = iter->next;
    }

    if (!to_delete) return; // Nothing to delete

    // Delete them
    for (GList *d = to_delete; d != NULL; d = d->next) {
        RuleData *rule = (RuleData *)d->data;
        ProxyBridge_DeleteRule(rule->id);
        g_rules_list = g_list_remove(g_rules_list, rule);
        free_rule_data(rule);
    }
    g_list_free(to_delete);
    
    refresh_rules_ui();
}

static void on_select_all_clicked(GtkWidget *widget, gpointer data) {
    if (!g_rules_list) return;
    
    // Check if currently all selected
    bool all_selected = true;
    for (GList *l = g_rules_list; l != NULL; l = l->next) {
        RuleData *r = (RuleData *)l->data;
        if (!r->selected) {
            all_selected = false;
            break;
        }
    }
    
    bool new_state = !all_selected; // Toggle
    
    for (GList *l = g_rules_list; l != NULL; l = l->next) {
        RuleData *r = (RuleData *)l->data;
        r->selected = new_state;
    }
    refresh_rules_ui();
}

static void refresh_rules_ui() {
    if (!rules_list_box) return;
    
    // Clear existing
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(rules_list_box));
    for(iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    // Check All status for button label
    if (btn_select_all_header) {
        bool all_selected = (g_rules_list != NULL);
        if (g_rules_list == NULL) all_selected = false;
        
        for (GList *l = g_rules_list; l != NULL; l = l->next) {
            RuleData *r = (RuleData *)l->data;
            if (!r->selected) {
                all_selected = false;
                break;
            }
        }
        gtk_button_set_label(GTK_BUTTON(btn_select_all_header), all_selected ? "Deselect All" : "Select All");
    }
    
    // Use GtkGrid for alignment
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 15);
    gtk_container_add(GTK_CONTAINER(rules_list_box), grid);

    // Headers
    // Col 0: Select (new)
    // Col 1: Enabled
    // Col 2: Actions
    // Col 3: SR
    // Col 4: Process
    // Col 5: Host
    // Col 6: Protocol
    // Col 7: Action
    
    int row = 0;
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("  "), 0, row, 1, 1); // Selection Header
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Enable"), 1, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Actions"), 2, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("SR"), 3, row, 1, 1);
    
    GtkWidget *h_proc = gtk_label_new("Process"); gtk_widget_set_halign(h_proc, GTK_ALIGN_START);
    gtk_widget_set_hexpand(h_proc, TRUE);
    gtk_grid_attach(GTK_GRID(grid), h_proc, 4, row, 1, 1);
    
    GtkWidget *h_host = gtk_label_new("Target Hosts"); gtk_widget_set_halign(h_host, GTK_ALIGN_START);
    gtk_widget_set_hexpand(h_host, TRUE);
    gtk_grid_attach(GTK_GRID(grid), h_host, 5, row, 1, 1);
    
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Protocol"), 6, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Action"), 7, row, 1, 1);
    
    // Separator
    row++;
    gtk_grid_attach(GTK_GRID(grid), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row, 8, 1);
    row++;

    // Data Rows
    int sr_counter = 1;
    for (GList *l = g_rules_list; l != NULL; l = l->next) {
        RuleData *r = (RuleData *)l->data;
        
        // Select Checkbox
        GtkWidget *chk_sel = gtk_check_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_sel), r->selected);
        g_signal_connect(chk_sel, "toggled", G_CALLBACK(on_rule_select_toggle), r);
        gtk_grid_attach(GTK_GRID(grid), chk_sel, 0, row, 1, 1);

        // Enabled
        GtkWidget *chk = gtk_check_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk), r->enabled);
        g_signal_connect(chk, "toggled", G_CALLBACK(on_rule_toggle), r);
        gtk_grid_attach(GTK_GRID(grid), chk, 1, row, 1, 1);
        
        // Actions
        GtkWidget *act_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        GtkWidget *btn_edit = gtk_button_new_with_label("Edit");
        g_signal_connect(btn_edit, "clicked", G_CALLBACK(on_rule_edit), r);
        GtkWidget *btn_del = gtk_button_new_with_label("Delete");
        g_signal_connect(btn_del, "clicked", G_CALLBACK(on_rule_delete), r);
        gtk_box_pack_start(GTK_BOX(act_box), btn_edit, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(act_box), btn_del, FALSE, FALSE, 0);
        gtk_grid_attach(GTK_GRID(grid), act_box, 2, row, 1, 1);
        
        // SR
        char sr_str[16]; snprintf(sr_str, sizeof(sr_str), "%d", sr_counter++);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(sr_str), 3, row, 1, 1);
        
        // Process
        GtkWidget *l_proc = gtk_label_new(r->process_name); 
        gtk_widget_set_halign(l_proc, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(l_proc), PANGO_ELLIPSIZE_END);
        gtk_grid_attach(GTK_GRID(grid), l_proc, 4, row, 1, 1);
        
        // Host
        GtkWidget *l_host = gtk_label_new(r->target_hosts); 
        gtk_widget_set_halign(l_host, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(l_host), PANGO_ELLIPSIZE_END);
        gtk_grid_attach(GTK_GRID(grid), l_host, 5, row, 1, 1);
        
        // Protocol
        const char* proto_strs[] = {"TCP", "UDP", "BOTH"};
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(proto_strs[r->protocol]), 6, row, 1, 1);
        
        // Action
        const char* action_strs[] = {"PROXY", "DIRECT", "BLOCK"};
        GtkWidget *l_act = gtk_label_new(action_strs[r->action]);
        // Set Color
        GtkStyleContext *context = gtk_widget_get_style_context(l_act);
        if (r->action == RULE_ACTION_PROXY) gtk_style_context_add_class(context, "success");
        else if (r->action == RULE_ACTION_DIRECT) gtk_style_context_add_class(context, "info");
        else gtk_style_context_add_class(context, "warning");
        
        gtk_grid_attach(GTK_GRID(grid), l_act, 7, row, 1, 1);
        
        row++;
        gtk_grid_attach(GTK_GRID(grid), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row, 8, 1);
        row++;
    }
    
    gtk_widget_show_all(rules_list_box);
}

static void on_proxy_rules_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(dialog), "Process Rules");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 800, 500);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window));
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_container_add(GTK_CONTAINER(dialog), vbox);
    
    // Header Row with Title and Add Button
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='x-large' weight='bold'>Process Rules</span>");
    
    GtkWidget *add_btn = gtk_button_new_with_label("+ Add Rule");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_rule_add_clicked), NULL);
    
    // Select All Button
    btn_select_all_header = gtk_button_new_with_label("Select All");
    g_signal_connect(btn_select_all_header, "clicked", G_CALLBACK(on_select_all_clicked), NULL);

    // Bulk Delete Button
    GtkWidget *del_all_btn = gtk_button_new_with_label("Delete Selected");
    g_signal_connect(del_all_btn, "clicked", G_CALLBACK(on_bulk_delete_clicked), NULL);

    // Import/Export Buttons
    GtkWidget *import_btn = gtk_button_new_with_label("Import");
    g_signal_connect(import_btn, "clicked", G_CALLBACK(on_rule_import_clicked), NULL);
    
    GtkWidget *export_btn = gtk_button_new_with_label("Export");
    g_signal_connect(export_btn, "clicked", G_CALLBACK(on_rule_export_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(header_box), title, FALSE, FALSE, 0);
    
    // Spacing
    GtkWidget *spacer = gtk_label_new("");
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_pack_start(GTK_BOX(header_box), spacer, TRUE, TRUE, 0);

    // Buttons (Packed End = Right to Left order on screen)
    gtk_box_pack_end(GTK_BOX(header_box), add_btn, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(header_box), btn_select_all_header, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(header_box), del_all_btn, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(header_box), export_btn, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(header_box), import_btn, FALSE, FALSE, 5);
    
    gtk_box_pack_start(GTK_BOX(vbox), header_box, FALSE, FALSE, 0);
    
    // Rules List Area
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolled, TRUE);
    rules_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(scrolled), rules_list_box);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);
    
    refresh_rules_ui();
    
    gtk_widget_show_all(dialog);
}

static void on_log_traffic_toggled(GtkCheckMenuItem *item, gpointer data) {
    bool active = gtk_check_menu_item_get_active(item);
    ProxyBridge_SetTrafficLoggingEnabled(active);
}

static void on_dns_proxy_toggled(GtkCheckMenuItem *item, gpointer data) {
    bool active = gtk_check_menu_item_get_active(item);
    ProxyBridge_SetDnsViaProxy(active);
}

static void on_about(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("About ProxyBridge",
                                                    GTK_WINDOW(window),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                                                    "OK", GTK_RESPONSE_OK,
                                                    NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 300);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 20);

    const char *markup = 
        "<span size='xx-large' weight='bold'>ProxyBridge</span>\n"
        "<span color='gray'>Version 4.0-Beta</span>\n\n"
        "Universal proxy client for Linux applications\n\n"
        "Author: Sourav Kalal / InterceptSuite\n\n"
        "Website: <a href=\"https://interceptsuite.com\">interceptsuite.com</a>\n"
        "GitHub: <a href=\"https://github.com/InterceptSuite/ProxyBridge\">InterceptSuite/ProxyBridge</a>\n\n"
        "License: MIT";

    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    
    gtk_box_pack_start(GTK_BOX(content_area), label, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void on_create_update_script_and_run() {
    // We use execl to replace the current process, so we must stop everything first
    ProxyBridge_Stop();
    
    const char *script_url = "https://raw.githubusercontent.com/InterceptSuite/ProxyBridge/refs/heads/master/Linux/deploy.sh";
    
    // Secure temp directory
    char tmp_dir_tpl[] = "/tmp/pb_update_XXXXXX";
    char *tmp_dir = mkdtemp(tmp_dir_tpl);
    if (!tmp_dir) {
        fprintf(stderr, "Failed to create temp directory for update.\n");
        exit(1);
    }
    
    char script_path[512];
    snprintf(script_path, sizeof(script_path), "%s/deploy.sh", tmp_dir);
    
    // Download using fork/exec of curl, safer than system()
    pid_t pid = fork();
    if (pid == -1) {
        fprintf(stderr, "Fork failed.\n");
        exit(1);
    } else if (pid == 0) {
        // Child
        execlp("curl", "curl", "-s", "-o", script_path, script_url, NULL);
        _exit(127);
    } else {
        // Parent
        int status;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fprintf(stderr, "Failed to download update script.\n");
            exit(1);
        }
    }
    
    // chmod +x
    if (chmod(script_path, S_IRWXU) != 0) {
        perror("chmod failed");
        exit(1);
    }
    
    // Replace process with the update script
    execl("/bin/bash", "bash", script_path, NULL);
    exit(0); // Should not reach here
}

static void on_check_update(GtkWidget *widget, gpointer data) {
    const char *url = "https://api.github.com/repos/InterceptSuite/ProxyBridge/releases/latest";
    char *cmd = g_strdup_printf("curl -s -H \"User-Agent: ProxyBridge-Linux\" %s", url);
    
    char *standard_output = NULL;
    char *standard_error = NULL;
    GError *error = NULL;
    int exit_status = 0;

    // Use GLib spawn instead of popen for safety and correct signal handling
    gboolean result = g_spawn_command_line_sync(cmd,
                                                &standard_output,
                                                &standard_error,
                                                &exit_status,
                                                &error);
    g_free(cmd);

    if (!result) {
         GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Failed to launch update check: %s", error ? error->message : "Unknown error");
         gtk_dialog_run(GTK_DIALOG(msg));
         gtk_widget_destroy(msg);
         if (error) g_error_free(error);
         return;
    }

    if (exit_status != 0 || !standard_output || strlen(standard_output) == 0) {
         GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Update check failed (Exit: %d).", exit_status);
         gtk_dialog_run(GTK_DIALOG(msg));
         gtk_widget_destroy(msg);
         g_free(standard_output);
         g_free(standard_error);
         return;
    }
    
    // Copy to buffer for existing logic (simplified)
    // Or just use standard_output directly
    char *tag_name = extract_sub_json_str(standard_output, "tag_name");
    g_free(standard_output);
    g_free(standard_error);

    if (!tag_name) {
         GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "Could not parse version info.\nResponse might be rate limited.");
         gtk_dialog_run(GTK_DIALOG(msg));
         gtk_widget_destroy(msg);
         return;
    }
    
    // Compare
    const char *current_ver = "v4.0.0"; 
    
    if (strcmp(tag_name, current_ver) == 0) {
         GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "You are using the latest version (%s).", current_ver);
         gtk_dialog_run(GTK_DIALOG(msg));
         gtk_widget_destroy(msg);
    } else {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_QUESTION,
                                            GTK_BUTTONS_NONE,
                                            "New version %s is available!\nCurrent version: %s\n\nDo you want to update now?", 
                                            tag_name, current_ver);
        gtk_dialog_add_button(GTK_DIALOG(dialog), "Download Now", GTK_RESPONSE_ACCEPT);
        gtk_dialog_add_button(GTK_DIALOG(dialog), "Close", GTK_RESPONSE_CANCEL);
        
        int resp = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        
        if (resp == GTK_RESPONSE_ACCEPT) {
             on_create_update_script_and_run();
        }
    }
    g_free(tag_name);
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

    // Force GSettings backend to 'memory' to prevent dconf/dbus-launch errors when running as root
    // This suppresses "failed to commit changes to dconf" warnings.
    setenv("GSETTINGS_BACKEND", "memory", 1);

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
    GtkWidget *rules_item = gtk_menu_item_new_with_label("Proxy Rules");
    
    // New Check Menu Items
    GtkWidget *log_check_item = gtk_check_menu_item_new_with_label("Enable Traffic Logging");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(log_check_item), TRUE); // Default
    g_signal_connect(log_check_item, "toggled", G_CALLBACK(on_log_traffic_toggled), NULL);

    GtkWidget *dns_check_item = gtk_check_menu_item_new_with_label("DNS via Proxy");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(dns_check_item), TRUE); // Default
    g_signal_connect(dns_check_item, "toggled", G_CALLBACK(on_dns_proxy_toggled), NULL);

    GtkWidget *exit_item = gtk_menu_item_new_with_label("Exit");
    
    g_signal_connect(config_item, "activate", G_CALLBACK(on_proxy_configure), NULL);
    g_signal_connect(rules_item, "activate", G_CALLBACK(on_proxy_rules_clicked), NULL);
    g_signal_connect(exit_item, "activate", G_CALLBACK(on_window_destroy), NULL);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(proxy_menu), config_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(proxy_menu), rules_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(proxy_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(proxy_menu), log_check_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(proxy_menu), dns_check_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(proxy_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(proxy_menu), exit_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(proxy_menu_item), proxy_menu);
    
    GtkWidget *about_menu_item = gtk_menu_item_new_with_label("About");
    GtkWidget *about_menu = gtk_menu_new();
    
    GtkWidget *about_child_item = gtk_menu_item_new_with_label("About");
    g_signal_connect(about_child_item, "activate", G_CALLBACK(on_about), NULL);
    
    GtkWidget *update_item = gtk_menu_item_new_with_label("Check for Updates");
    g_signal_connect(update_item, "activate", G_CALLBACK(on_check_update), NULL);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(about_menu), about_child_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(about_menu), update_item);
    
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(about_menu_item), about_menu);

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
