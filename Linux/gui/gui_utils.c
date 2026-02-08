#include "gui.h"

// safer way to turn string to int
long safe_strtol(const char *nptr) {
    if (!nptr) return 0; // null check
    char *endptr;
    long val = strtol(nptr, &endptr, 10);
    if (endptr == nptr) return 0; // bad input
    return val;
}

// show popup msg
void show_message(GtkWindow *parent, GtkMessageType type, const char *format, ...) {
    va_list args;
    va_start(args, format);
    char *msg = g_strdup_vprintf(format, args);
    va_end(args);

    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                            GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                                            type,
                                            GTK_BUTTONS_OK,
                                            "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(msg);
}

// keep log size under control so we dont use too much ram
void trim_buffer_lines(GtkTextBuffer *buffer, int max_lines) {
    if (gtk_text_buffer_get_line_count(buffer) > max_lines) {
        GtkTextIter start, next;
        gtk_text_buffer_get_start_iter(buffer, &start);
        next = start;
        gtk_text_iter_forward_line(&next);
        gtk_text_buffer_delete(buffer, &start, &next);
    }
}

char* get_current_time_str() {
    time_t rawtime;
    struct tm *timeinfo;
    char *buffer = malloc(32);
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, 32, "[%H:%M:%S]", timeinfo);
    return buffer;
}

// json stuff
char *escape_json_string(const char *src) {
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

// basic parser, good enough for valid inputs
char *extract_sub_json_str(const char *json, const char *key) {
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

bool extract_sub_json_bool(const char *json, const char *key) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    char *k = strstr(json, search_key);
    if (!k) return false;
    char *colon = strchr(k, ':');
    if (!colon) return false;
    // skip whitespace
    char *v = colon + 1;
    while(*v == ' ' || *v == '\t') v++;
    if (strncmp(v, "true", 4) == 0) return true;
    return false;
}
