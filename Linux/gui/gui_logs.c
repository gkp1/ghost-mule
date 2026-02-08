#include "gui.h"

// filter logs based on search
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

        // search case insensitive
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

void on_search_conn_changed(GtkSearchEntry *entry, gpointer user_data) {
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
    filter_text_view(conn_buffer, text);
}

void on_search_log_changed(GtkSearchEntry *entry, gpointer user_data) {
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
    filter_text_view(log_buffer, text);
}

void on_clear_conn_clicked(GtkButton *button, gpointer user_data) {
    if (conn_buffer) gtk_text_buffer_set_text(conn_buffer, "", 0);
}

void on_clear_log_clicked(GtkButton *button, gpointer user_data) {
    if (log_buffer) gtk_text_buffer_set_text(log_buffer, "", 0);
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

// update logs from main thread
static gboolean update_log_gui(gpointer user_data) {
    LogData *data = (LogData *)user_data;
    if (!data) return FALSE;

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(log_buffer, &end);

    char *time_str = get_current_time_str();
    char full_msg[1200];
    snprintf(full_msg, sizeof(full_msg), "%s %s\n", time_str, data->message);
    free(time_str);

    gtk_text_buffer_insert(log_buffer, &end, full_msg, -1);

    trim_buffer_lines(log_buffer, 100);

    free(data->message);
    free(data);
    return FALSE; // done
}

// update conn info
static gboolean update_connection_gui_append(gpointer user_data) {
    ConnectionData *data = (ConnectionData *)user_data;
    if (!data) return FALSE;

    if (conn_buffer) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(conn_buffer, &end);

        char line_buffer[1024];
        snprintf(line_buffer, sizeof(line_buffer), "%s %s (PID:%u) -> %s:%u via %s\n",
                 data->timestamp, data->process_name, data->pid, data->dest_ip, data->dest_port, data->proxy_info);

        gtk_text_buffer_insert(conn_buffer, &end, line_buffer, -1);

        trim_buffer_lines(conn_buffer, 100);
    }

    free_connection_data(data);
    return FALSE;
}

void lib_log_callback(const char *message) {
    LogData *data = malloc(sizeof(LogData));
    data->message = strdup(message);
    g_idle_add(update_log_gui, data);
}

void lib_connection_callback(const char *process_name, uint32_t pid, const char *dest_ip, uint16_t dest_port, const char *proxy_info) {
    ConnectionData *data = malloc(sizeof(ConnectionData));
    data->process_name = strdup(process_name);
    data->pid = pid;
    data->dest_ip = strdup(dest_ip);
    data->dest_port = dest_port;
    data->proxy_info = strdup(proxy_info);
    data->timestamp = get_current_time_str();
    g_idle_add(update_connection_gui_append, data);
}
