#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <ctype.h>
#include "../src/ProxyBridge.h"

#define MAX_RULES 100
#define MAX_RULE_STR 512

typedef struct {
    char process_name[256];
    char target_hosts[256];
    char target_ports[256];
    RuleProtocol protocol;
    RuleAction action;
} ProxyRule;

static volatile bool keep_running = false;
static int verbose_level = 0;

static void log_callback(const char* message)
{
    if (verbose_level == 1 || verbose_level == 3)
    {
        printf("[LOG] %s\n", message);
    }
}

static void connection_callback(const char* process_name, uint32_t pid, const char* dest_ip, uint16_t dest_port, const char* proxy_info)
{
    if (verbose_level == 2 || verbose_level == 3)
    {
        printf("[CONN] %s (PID:%u) -> %s:%u via %s\n",
               process_name, pid, dest_ip, dest_port, proxy_info);
    }
}

static void signal_handler(int sig)
{
    if (sig == SIGSEGV || sig == SIGABRT || sig == SIGBUS)
    {
        printf("\n\n=== CLI CRASH DETECTED ===\n");
        printf("Signal: %d (%s)\n", sig,
               sig == SIGSEGV ? "SEGFAULT" :
               sig == SIGABRT ? "ABORT" : "BUS ERROR");
        printf("Calling emergency cleanup...\n");
        ProxyBridge_Stop();
        _exit(1);
    }

    if (keep_running)
    {
        printf("\n\nStopping ProxyBridge...\n");
        keep_running = false;
    }
}

static void show_banner(void)
{
    printf("\n");
    printf("  ____                        ____       _     _            \n");
    printf(" |  _ \\ _ __ _____  ___   _  | __ ) _ __(_) __| | __ _  ___ \n");
    printf(" | |_) | '__/ _ \\ \\/ / | | | |  _ \\| '__| |/ _` |/ _` |/ _ \\\n");
    printf(" |  __/| | | (_) >  <| |_| | | |_) | |  | | (_| | (_| |  __/\n");
    printf(" |_|   |_|  \\___/_/\\_\\\\__, | |____/|_|  |_|\\__,_|\\__, |\\___|\n");
    printf("                      |___/                      |___/  V4.0-Beta\n");
    printf("\n");
    printf("  Universal proxy client for Linux applications\n");
    printf("\n");
    printf("\tAuthor: Sourav Kalal/InterceptSuite\n");
    printf("\tGitHub: https://github.com/InterceptSuite/ProxyBridge\n");
    printf("\n");
}

static void show_help(const char* prog)
{
    show_banner();
    printf("USAGE:\n");
    printf("  %s [OPTIONS]\n\n", prog);

    printf("OPTIONS:\n");
    printf("  --proxy <url>          Proxy server URL with optional authentication\n");
    printf("                         Format: type://ip:port or type://ip:port:username:password\n");
    printf("                         Examples: socks5://127.0.0.1:1080\n");
    printf("                                   http://proxy.com:8080:myuser:mypass\n");
    printf("                         Default: socks5://127.0.0.1:4444\n\n");

    printf("  --rule <rule>          Traffic routing rule (can be specified multiple times)\n");
    printf("                         Format: process:hosts:ports:protocol:action\n");
    printf("                           process  - Process name(s): curl, cur*, *, or multiple separated by ;\n");
    printf("                           hosts    - IP/host(s): *, google.com, 192.168.*.*, or multiple separated by ; or ,\n");
    printf("                           ports    - Port(s): *, 443, 80;8080, 80-100, or multiple separated by ; or ,\n");
    printf("                           protocol - TCP, UDP, or BOTH\n");
    printf("                           action   - PROXY, DIRECT, or BLOCK\n");
    printf("                         Examples:\n");
    printf("                           curl:*:*:TCP:PROXY\n");
    printf("                           curl;wget:*:*:TCP:PROXY\n");
    printf("                           *:*:53:UDP:PROXY\n");
    printf("                           firefox:*:80;443:TCP:DIRECT\n\n");

    printf("  --dns-via-proxy <bool> Route DNS queries through proxy\n");
    printf("                         Values: true, false, 1, 0\n");
    printf("                         Default: true\n\n");

    printf("  --verbose <level>      Logging verbosity level\n");
    printf("                           0 - No logs (default)\n");
    printf("                           1 - Show log messages only\n");
    printf("                           2 - Show connection events only\n");
    printf("                           3 - Show both logs and connections\n\n");

    printf("  --cleanup              Cleanup resources (iptables, etc.) from crashed instance\n");
    printf("                         Use if ProxyBridge crashed without proper cleanup\n\n");

    printf("  --help, -h             Show this help message\n\n");

    printf("EXAMPLES:\n");
    printf("  # Basic usage with default proxy\n");
    printf("  sudo %s --rule curl:*:*:TCP:PROXY\n\n", prog);

    printf("  # Multiple rules with custom proxy\n");
    printf("  sudo %s --proxy socks5://192.168.1.10:1080 \\\n", prog);
    printf("       --rule curl:*:*:TCP:PROXY \\\n");
    printf("       --rule wget:*:*:TCP:PROXY \\\n");
    printf("       --verbose 2\n\n");

    printf("  # Route DNS through proxy with multiple apps\n");
    printf("  sudo %s --proxy socks5://127.0.0.1:1080 \\\n", prog);
    printf("       --rule \"curl;wget;firefox:*:*:BOTH:PROXY\" \\\n");
    printf("       --dns-via-proxy true --verbose 3\n\n");

    printf("NOTE:\n");
    printf("  ProxyBridge requires root privileges to use nfqueue.\n");
    printf("  Run with 'sudo' or as root user.\n\n");
}

static RuleProtocol parse_protocol(const char* str)
{
    char upper[16];
    for (size_t i = 0; str[i] && i < 15; i++)
        upper[i] = toupper(str[i]);
    upper[strlen(str) < 15 ? strlen(str) : 15] = '\0';

    if (strcmp(upper, "TCP") == 0)
        return RULE_PROTOCOL_TCP;
    else if (strcmp(upper, "UDP") == 0)
        return RULE_PROTOCOL_UDP;
    else if (strcmp(upper, "BOTH") == 0)
        return RULE_PROTOCOL_BOTH;
    else
    {
        fprintf(stderr, "ERROR: Invalid protocol '%s'. Use TCP, UDP, or BOTH\n", str);
        exit(1);
    }
}

static RuleAction parse_action(const char* str)
{
    char upper[16];
    for (size_t i = 0; str[i] && i < 15; i++)
        upper[i] = toupper(str[i]);
    upper[strlen(str) < 15 ? strlen(str) : 15] = '\0';

    if (strcmp(upper, "PROXY") == 0)
        return RULE_ACTION_PROXY;
    else if (strcmp(upper, "DIRECT") == 0)
        return RULE_ACTION_DIRECT;
    else if (strcmp(upper, "BLOCK") == 0)
        return RULE_ACTION_BLOCK;
    else
    {
        fprintf(stderr, "ERROR: Invalid action '%s'. Use PROXY, DIRECT, or BLOCK\n", str);
        exit(1);
    }
}

static void default_if_empty(char* dest, const char* src, const char* default_val, size_t dest_size)
{
    if (src == NULL || src[0] == '\0' || strcmp(src, " ") == 0)
        strncpy(dest, default_val, dest_size - 1);
    else
        strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static bool parse_rule(const char* rule_str, ProxyRule* rule)
{
    char buffer[MAX_RULE_STR];
    strncpy(buffer, rule_str, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* parts[5] = {NULL, NULL, NULL, NULL, NULL};
    int part_idx = 0;
    char* token = strtok(buffer, ":");

    while (token != NULL && part_idx < 5)
    {
        parts[part_idx++] = token;
        token = strtok(NULL, ":");
    }

    if (part_idx != 5)
    {
        fprintf(stderr, "ERROR: Invalid rule format '%s'\n", rule_str);
        fprintf(stderr, "Expected format: process:hosts:ports:protocol:action\n");
        return false;
    }

    default_if_empty(rule->process_name, parts[0], "*", sizeof(rule->process_name));
    default_if_empty(rule->target_hosts, parts[1], "*", sizeof(rule->target_hosts));
    default_if_empty(rule->target_ports, parts[2], "*", sizeof(rule->target_ports));

    rule->protocol = parse_protocol(parts[3]);
    rule->action = parse_action(parts[4]);

    return true;
}

static bool parse_proxy_url(const char* url, ProxyType* type, char* host, uint16_t* port, char* username, char* password)
{
    char buffer[512];
    strncpy(buffer, url, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    username[0] = '\0';
    password[0] = '\0';

    // parse type://
    char* scheme_end = strstr(buffer, "://");
    if (scheme_end == NULL)
    {
        fprintf(stderr, "ERROR: Invalid proxy URL format. Expected type://host:port\n");
        return false;
    }

    *scheme_end = '\0';
    char* scheme = buffer;
    char* rest = scheme_end + 3;

    char upper_scheme[16];
    for (size_t i = 0; scheme[i] && i < 15; i++)
        upper_scheme[i] = toupper(scheme[i]);
    upper_scheme[strlen(scheme) < 15 ? strlen(scheme) : 15] = '\0';

    if (strcmp(upper_scheme, "SOCKS5") == 0)
        *type = PROXY_TYPE_SOCKS5;
    else if (strcmp(upper_scheme, "HTTP") == 0)
        *type = PROXY_TYPE_HTTP;
    else
    {
        fprintf(stderr, "ERROR: Invalid proxy type '%s'. Use 'socks5' or 'http'\n", scheme);
        return false;
    }

    // parse host:port[:user:pass]
    char* parts[4];
    int num_parts = 0;
    char* token = strtok(rest, ":");
    while (token != NULL && num_parts < 4)
    {
        parts[num_parts++] = token;
        token = strtok(NULL, ":");
    }

    if (num_parts < 2)
    {
        fprintf(stderr, "ERROR: Invalid proxy URL. Missing host or port\n");
        return false;
    }

    strncpy(host, parts[0], 255);
    host[255] = '\0';

    *port = atoi(parts[1]);
    if (*port == 0)
    {
        fprintf(stderr, "ERROR: Invalid proxy port '%s'\n", parts[1]);
        return false;
    }

    if (num_parts >= 4)
    {
        strncpy(username, parts[2], 255);
        username[255] = '\0';
        strncpy(password, parts[3], 255);
        password[255] = '\0';
    }

    return true;
}

static bool is_root(void)
{
    return getuid() == 0;
}

int main(int argc, char *argv[])
{
    // check cleanup flag
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--cleanup") == 0)
        {
            printf("Running cleanup...\n");
            ProxyBridge_Stop();
            printf("Cleanup complete.\n");
            return 0;
        }
    }

    char proxy_url[512] = "socks5://127.0.0.1:4444";
    ProxyRule rules[MAX_RULES];
    int num_rules = 0;
    bool dns_via_proxy = true;

    // parse args
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            show_help(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "--proxy") == 0 && i + 1 < argc)
        {
            strncpy(proxy_url, argv[++i], sizeof(proxy_url) - 1);
            proxy_url[sizeof(proxy_url) - 1] = '\0';
        }
        else if (strcmp(argv[i], "--rule") == 0 && i + 1 < argc)
        {
            if (num_rules >= MAX_RULES)
            {
                fprintf(stderr, "ERROR: Maximum %d rules supported\n", MAX_RULES);
                return 1;
            }
            if (!parse_rule(argv[++i], &rules[num_rules]))
                return 1;
            num_rules++;
        }
        else if (strcmp(argv[i], "--dns-via-proxy") == 0 && i + 1 < argc)
        {
            char* value = argv[++i];
            if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0)
                dns_via_proxy = true;
            else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0)
                dns_via_proxy = false;
            else
            {
                fprintf(stderr, "ERROR: Invalid value for --dns-via-proxy. Use: true, false, 1, or 0\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--verbose") == 0 && i + 1 < argc)
        {
            verbose_level = atoi(argv[++i]);
            if (verbose_level < 0 || verbose_level > 3)
            {
                fprintf(stderr, "ERROR: Verbose level must be 0-3\n");
                return 1;
            }
        }
        else
        {
            fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
            fprintf(stderr, "Use --help for usage information\n");
            return 1;
        }
    }

    show_banner();

    // need root
    if (!is_root())
    {
        printf("\033[31m\nERROR: ProxyBridge requires root privileges!\033[0m\n");
        printf("Please run this application with sudo or as root.\n\n");
        return 1;
    }

    // parse proxy config
    ProxyType proxy_type;
    char proxy_host[256];
    uint16_t proxy_port;
    char proxy_username[256];
    char proxy_password[256];

    if (!parse_proxy_url(proxy_url, &proxy_type, proxy_host, &proxy_port, proxy_username, proxy_password))
        return 1;

    // setup callbacks based on verbose
    // 0=nothing 1=logs 2=connections 3=both

    if (verbose_level == 1 || verbose_level == 3)
        ProxyBridge_SetLogCallback(log_callback);
    else
        ProxyBridge_SetLogCallback(NULL);  // Explicitly disable

    if (verbose_level == 2 || verbose_level == 3)
        ProxyBridge_SetConnectionCallback(connection_callback);
    else
        ProxyBridge_SetConnectionCallback(NULL);  // Explicitly disable

    // turn on traffic logging when needed
    ProxyBridge_SetTrafficLoggingEnabled(verbose_level > 0);

    // show config
    printf("Proxy: %s://%s:%u\n",
           proxy_type == PROXY_TYPE_HTTP ? "http" : "socks5",
           proxy_host, proxy_port);

    if (proxy_username[0] != '\0')
        printf("Proxy Auth: %s:***\n", proxy_username);

    printf("DNS via Proxy: %s\n", dns_via_proxy ? "Enabled" : "Disabled");

    // setup proxy
    if (!ProxyBridge_SetProxyConfig(proxy_type, proxy_host, proxy_port,
                                    proxy_username[0] ? proxy_username : "",
                                    proxy_password[0] ? proxy_password : ""))
    {
        fprintf(stderr, "ERROR: Failed to set proxy configuration\n");
        return 1;
    }

    ProxyBridge_SetDnsViaProxy(dns_via_proxy);

    // add rules
    if (num_rules > 0)
    {
        printf("Rules: %d\n", num_rules);
        for (int i = 0; i < num_rules; i++)
        {
            const char* protocol_str = rules[i].protocol == RULE_PROTOCOL_TCP ? "TCP" :
                                      rules[i].protocol == RULE_PROTOCOL_UDP ? "UDP" : "BOTH";
            const char* action_str = rules[i].action == RULE_ACTION_PROXY ? "PROXY" :
                                    rules[i].action == RULE_ACTION_DIRECT ? "DIRECT" : "BLOCK";

            uint32_t rule_id = ProxyBridge_AddRule(
                rules[i].process_name,
                rules[i].target_hosts,
                rules[i].target_ports,
                rules[i].protocol,
                rules[i].action);

            if (rule_id > 0)
            {
                printf("  [%u] %s:%s:%s:%s -> %s\n",
                       rule_id,
                       rules[i].process_name,
                       rules[i].target_hosts,
                       rules[i].target_ports,
                       protocol_str,
                       action_str);
            }
            else
            {
                fprintf(stderr, "  ERROR: Failed to add rule for %s\n", rules[i].process_name);
            }
        }
    }
    else
    {
        printf("\033[33mWARNING: No rules specified. No traffic will be proxied.\033[0m\n");
        printf("Use --rule to add proxy rules. See --help for examples.\n");
    }

    // start proxybridge
    if (!ProxyBridge_Start())
    {
        fprintf(stderr, "ERROR: Failed to start ProxyBridge\n");
        return 1;
    }

    keep_running = true;
    printf("\nProxyBridge started. Press Ctrl+C to stop...\n\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGSEGV, signal_handler);  // Catch segfault
    signal(SIGABRT, signal_handler);  // Catch abort
    signal(SIGBUS, signal_handler);   // Catch bus error

    // main loop
    while (keep_running)
    {
        sleep(1);
    }

    // cleanup
    ProxyBridge_Stop();
    printf("ProxyBridge stopped.\n");

    return 0;
}
