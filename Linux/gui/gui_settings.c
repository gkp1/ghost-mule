#include "gui.h"

// settings stuff

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

    // run test
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

    // check inputs
    const char *ip_text = gtk_entry_get_text(GTK_ENTRY(info->ip_entry));
    const char *port_text = gtk_entry_get_text(GTK_ENTRY(info->port_entry));

    if (!ip_text || strlen(ip_text) == 0 || strspn(port_text, "0123456789") != strlen(port_text) || strlen(port_text) == 0) {
        gtk_text_buffer_set_text(info->output_buffer, "Error: Invalid Proxy IP or Port.", -1);
        return;
    }

    // save config
    ProxyType type = (gtk_combo_box_get_active(GTK_COMBO_BOX(info->type_combo)) == 0) ? PROXY_TYPE_HTTP : PROXY_TYPE_SOCKS5;
    int port = (int)safe_strtol(port_text);
    const char *user = gtk_entry_get_text(GTK_ENTRY(info->user_entry));
    const char *pass = gtk_entry_get_text(GTK_ENTRY(info->pass_entry));

    ProxyBridge_SetProxyConfig(type, ip_text, port, user, pass);

    // get target
    const char *t_host = gtk_entry_get_text(GTK_ENTRY(info->test_host));
    const char *t_port_s = gtk_entry_get_text(GTK_ENTRY(info->test_port));

    if (!t_host || strlen(t_host) == 0) t_host = "google.com";
    int t_port = (int)safe_strtol(t_port_s);
    if (t_port <= 0) t_port = 80;

    // update gui
    gtk_text_buffer_set_text(info->output_buffer, "Testing connection... Please wait...", -1);
    gtk_widget_set_sensitive(info->test_btn, FALSE);

    // start thread
    struct TestRunnerData *req = malloc(sizeof(struct TestRunnerData));
    req->host = strdup(t_host);
    req->port = t_port;
    req->ui_info = info;

    GThread *thread = g_thread_new("test_conn", run_test_thread, req);
    g_thread_unref(thread);
}

void on_proxy_configure(GtkWidget *widget, gpointer data) {
    ConfigInfo info;

    GtkWidget *content_area;
    GtkWidget *grid;

    info.dialog = gtk_dialog_new_with_buttons("Proxy Settings",
                                         GTK_WINDOW(window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "Cancel", GTK_RESPONSE_CANCEL,
                                         "Save", GTK_RESPONSE_ACCEPT,
                                         NULL);
    // wider window
    gtk_window_set_default_size(GTK_WINDOW(info.dialog), 600, 500);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(info.dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_box_pack_start(GTK_BOX(content_area), grid, TRUE, TRUE, 0);

    // proxy type dropdown
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Type:"), 0, 0, 1, 1);
    info.type_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(info.type_combo), "HTTP");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(info.type_combo), "SOCKS5");
    gtk_combo_box_set_active(GTK_COMBO_BOX(info.type_combo), g_proxy_type == PROXY_TYPE_HTTP ? 0 : 1);
    gtk_grid_attach(GTK_GRID(grid), info.type_combo, 1, 0, 3, 1);

    // ip field
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Host:"), 0, 1, 1, 1);
    info.ip_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(info.ip_entry), g_proxy_ip);
    gtk_widget_set_hexpand(info.ip_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), info.ip_entry, 1, 1, 3, 1);

    // port field
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Port:"), 0, 2, 1, 1);
    info.port_entry = gtk_entry_new();
    if (g_proxy_port != 0) {
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", g_proxy_port);
        gtk_entry_set_text(GTK_ENTRY(info.port_entry), port_str);
    }
    gtk_grid_attach(GTK_GRID(grid), info.port_entry, 1, 2, 3, 1);

    // user field
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Username:"), 0, 3, 1, 1);
    info.user_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(info.user_entry), g_proxy_user);
    gtk_grid_attach(GTK_GRID(grid), info.user_entry, 1, 3, 3, 1);

    // password
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Password:"), 0, 4, 1, 1);
    info.pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(info.pass_entry), FALSE);
    gtk_entry_set_text(GTK_ENTRY(info.pass_entry), g_proxy_pass);
    gtk_grid_attach(GTK_GRID(grid), info.pass_entry, 1, 4, 3, 1);

    // test section
    gtk_grid_attach(GTK_GRID(grid), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, 5, 4, 1);

    GtkWidget *test_label = gtk_label_new("<b>Test Connection</b>");
    gtk_label_set_use_markup(GTK_LABEL(test_label), TRUE);
    gtk_widget_set_halign(test_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), test_label, 0, 6, 4, 1);

    // target
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Target:"), 0, 7, 1, 1);
    info.test_host = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(info.test_host), "google.com");
    gtk_grid_attach(GTK_GRID(grid), info.test_host, 1, 7, 1, 1);

    // target port
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Port:"), 2, 7, 1, 1);
    info.test_port = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(info.test_port), "80");
    gtk_widget_set_size_request(info.test_port, 80, -1);
    gtk_grid_attach(GTK_GRID(grid), info.test_port, 3, 7, 1, 1);

    // test button
    info.test_btn = gtk_button_new_with_label("Start Test");
    g_signal_connect(info.test_btn, "clicked", G_CALLBACK(on_start_test_clicked), &info);
    gtk_grid_attach(GTK_GRID(grid), info.test_btn, 0, 8, 4, 1);

    // output log
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

        // check
        const char *ip_text = gtk_entry_get_text(GTK_ENTRY(info.ip_entry));
        const char *port_text = gtk_entry_get_text(GTK_ENTRY(info.port_entry));

        if (!ip_text || strlen(ip_text) == 0) {
            show_message(GTK_WINDOW(info.dialog), GTK_MESSAGE_ERROR, "Host (IP/Domain) cannot be empty.");
            continue;
        }

        if (strspn(port_text, "0123456789") != strlen(port_text) || strlen(port_text) == 0) {
            show_message(GTK_WINDOW(info.dialog), GTK_MESSAGE_ERROR, "Port must be a valid number.");
            continue;
        }

        int p = (int)safe_strtol(port_text);
        if (p < 1 || p > 65535) {
            show_message(GTK_WINDOW(info.dialog), GTK_MESSAGE_ERROR, "Port must be between 1 and 65535.");
             continue;
        }

        // save
        g_proxy_type = (gtk_combo_box_get_active(GTK_COMBO_BOX(info.type_combo)) == 0) ? PROXY_TYPE_HTTP : PROXY_TYPE_SOCKS5;
        g_strlcpy(g_proxy_ip, ip_text, sizeof(g_proxy_ip));
        g_proxy_port = p;
        g_strlcpy(g_proxy_user, gtk_entry_get_text(GTK_ENTRY(info.user_entry)), sizeof(g_proxy_user));
        g_strlcpy(g_proxy_pass, gtk_entry_get_text(GTK_ENTRY(info.pass_entry)), sizeof(g_proxy_pass));

        ProxyBridge_SetProxyConfig(g_proxy_type, g_proxy_ip, g_proxy_port, g_proxy_user, g_proxy_pass);

        save_config();

        char status_msg[512];
        snprintf(status_msg, sizeof(status_msg), "Configuration updated: %s:%d", g_proxy_ip, g_proxy_port);
        gtk_statusbar_push(GTK_STATUSBAR(status_bar), status_context_id, status_msg);
        break;
    }

    gtk_widget_destroy(info.dialog);
}
