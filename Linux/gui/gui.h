#ifndef PROXYBRIDGE_GUI_H
#define PROXYBRIDGE_GUI_H

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

struct TestRunnerData {
    char *host;
    uint16_t port;
    ConfigInfo *ui_info;
};

typedef struct {
    char *result_text;
    GtkTextBuffer *buffer;
    GtkWidget *btn;
} TestResultData;

extern GtkWidget *window;
extern GtkTextBuffer *conn_buffer;
extern GtkTextBuffer *log_buffer;
extern GtkWidget *status_bar;
extern guint status_context_id;

extern char g_proxy_ip[256];
extern uint16_t g_proxy_port;
extern ProxyType g_proxy_type;
extern char g_proxy_user[256];
extern char g_proxy_pass[256];

extern GList *g_rules_list;
extern bool g_chk_logging;
extern bool g_chk_dns;

long safe_strtol(const char *nptr);
void show_message(GtkWindow *parent, GtkMessageType type, const char *format, ...);
void trim_buffer_lines(GtkTextBuffer *buffer, int max_lines);
char* get_current_time_str();
char *escape_json_string(const char *src);
char *extract_sub_json_str(const char *json, const char *key);
bool extract_sub_json_bool(const char *json, const char *key);

// config storage
void save_config();
void load_config();

// settings
void on_proxy_configure(GtkWidget *widget, gpointer data);

// rules section
void on_proxy_rules_clicked(GtkWidget *widget, gpointer data);

// Logs
void lib_log_callback(const char *message);
void lib_connection_callback(const char *process_name, uint32_t pid, const char *dest_ip, uint16_t dest_port, const char *proxy_info);
void on_search_conn_changed(GtkSearchEntry *entry, gpointer user_data);
void on_search_log_changed(GtkSearchEntry *entry, gpointer user_data);
void on_clear_conn_clicked(GtkButton *button, gpointer user_data);
void on_clear_log_clicked(GtkButton *button, gpointer user_data);

#endif
