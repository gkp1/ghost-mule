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

// --- Data Structures ---

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

// --- Thread Communication ---
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

#endif // PROXYBRIDGE_GUI_H
