#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include "../src/ProxyBridge.h"

static volatile bool keep_running = true;

static void log_callback(const char* message)
{
    printf("[LOG] %s\n", message);
}

static void connection_callback(const char* process_name, uint32_t pid, const char* dest_ip, uint16_t dest_port, const char* proxy_info)
{
    printf("[CONN] %s (PID: %u) -> %s:%u via %s\n", 
           process_name, pid, dest_ip, dest_port, proxy_info);
}

static void signal_handler(int sig)
{
    (void)sig;
    keep_running = false;
}

static void print_usage(const char* prog)
{
    printf("usage: %s [options]\n", prog);
    printf("options:\n");
    printf("  -h <proxy_host>    proxy server hostname or ip (default: 127.0.0.1)\n");
    printf("  -p <proxy_port>    proxy server port (default: 1080)\n");
    printf("  -t <type>          proxy type: socks5 or http (default: socks5)\n");
    printf("  -u <username>      proxy username (optional)\n");
    printf("  -w <password>      proxy password (optional)\n");
    printf("  -d                 disable dns via proxy\n");
    printf("  --help             show this help\n");
    printf("\nexample:\n");
    printf("  %s -h 127.0.0.1 -p 1080 -t socks5\n", prog);
}

int main(int argc, char *argv[])
{
    char proxy_host[256] = "192.168.1.4";
    uint16_t proxy_port = 4444;
    ProxyType proxy_type = PROXY_TYPE_SOCKS5;
    char username[256] = "";
    char password[256] = "";
    bool dns_via_proxy = true;

    // parse args
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc)
        {
            strncpy(proxy_host, argv[++i], sizeof(proxy_host) - 1);
        }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
        {
            proxy_port = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
        {
            if (strcmp(argv[i + 1], "http") == 0)
                proxy_type = PROXY_TYPE_HTTP;
            else if (strcmp(argv[i + 1], "socks5") == 0)
                proxy_type = PROXY_TYPE_SOCKS5;
            i++;
        }
        else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc)
        {
            strncpy(username, argv[++i], sizeof(username) - 1);
        }
        else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc)
        {
            strncpy(password, argv[++i], sizeof(password) - 1);
        }
        else if (strcmp(argv[i], "-d") == 0)
        {
            dns_via_proxy = false;
        }
    }

    printf("proxybridge cli test\n");
    printf("proxy: %s://%s:%u\n", proxy_type == PROXY_TYPE_HTTP ? "http" : "socks5", 
           proxy_host, proxy_port);

    // setup callbacks
    ProxyBridge_SetLogCallback(log_callback);
    ProxyBridge_SetConnectionCallback(connection_callback);

    // configure proxy
    if (!ProxyBridge_SetProxyConfig(proxy_type, proxy_host, proxy_port, 
                                    username[0] ? username : NULL,
                                    password[0] ? password : NULL))
    {
        fprintf(stderr, "failed to configure proxy\n");
        return 1;
    }

    ProxyBridge_SetDnsViaProxy(dns_via_proxy);
    ProxyBridge_SetTrafficLoggingEnabled(true);

    // add rule - curl tcp traffic via proxy
    uint32_t rule_id = ProxyBridge_AddRule("curl", "*", "*", RULE_PROTOCOL_TCP, RULE_ACTION_PROXY);
    if (rule_id == 0)
    {
        fprintf(stderr, "failed to add curl rule\n");
        return 1;
    }
    printf("added rule id %u: curl tcp traffic via proxy\n", rule_id);

    // test connection
    char test_result[2048];
    printf("\ntesting proxy connection\n");
    if (ProxyBridge_TestConnection("www.google.com", 80, test_result, sizeof(test_result)) == 0)
    {
        printf("%s\n", test_result);
    }
    else
    {
        printf("connection test failed:\n%s\n", test_result);
        return 1;
    }

    // start service
    printf("\nstarting proxybridge\n");
    printf("note: requires root privileges for nfqueue\n");
    
    if (getuid() != 0)
    {
        fprintf(stderr, "error: must run as root\n");
        return 1;
    }

    if (!ProxyBridge_Start())
    {
        fprintf(stderr, "failed to start proxybridge\n");
        return 1;
    }

    printf("proxybridge running - press ctrl+c to stop\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // main loop
    while (keep_running)
    {
        sleep(1);
    }

    printf("\nstopping proxybridge\n");
    ProxyBridge_Stop();

    printf("cleanup completed\n");
    return 0;
}
