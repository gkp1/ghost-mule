# ProxyBridge for Linux

Universal proxy client for Linux applications - Route any application through SOCKS5/HTTP proxies.

## Table of Contents

- [Installation](#installation)
  - [Quick Install (Recommended)](#quick-install-recommended)
  - [Manual Installation](#manual-installation)
- [Usage](#usage)
  - [GUI Application](#gui-application)
  - [Command Line Interface (CLI)](#command-line-interface-cli)
    - [Basic Usage](#basic-usage)
    - [Command Line Options](#command-line-options)
    - [Rule Format](#rule-format)
- [Use Cases](#use-cases)
- [Current Limitations](#current-limitations)
- [Things to Note](#things-to-note)
- [How It Works](#how-it-works)
- [Build from Source](#build-from-source)
- [Uninstallation](#uninstallation)
- [License](#license)

## Installation

### Quick Install (Recommended)

**One-command automatic installation:**

```bash
curl -Lo deploy.sh https://raw.githubusercontent.com/InterceptSuite/ProxyBridge/refs/heads/master/Linux/deploy.sh && sudo bash deploy.sh
```

This script will:
- Download the latest release from GitHub automatically
- Detect your Linux distribution
- Install all required dependencies (libnetfilter-queue, iptables, GTK3)
- Install ProxyBridge CLI, GUI, and library to system paths
- Update library cache (ldconfig)
- Verify installation

**Supported distributions:**
- Debian/Ubuntu/Mint/Pop!_OS/Elementary/Zorin/Kali/Raspbian
- Fedora
- RHEL/CentOS/Rocky/AlmaLinux
- Arch/Manjaro/EndeavourOS/Garuda
- openSUSE/SLES
- Void Linux

### Manual Installation

**From GitHub Releases:**

1. Download the latest `ProxyBridge-Linux-vX.X.X.tar.gz` from the [Releases](https://github.com/InterceptSuite/ProxyBridge/releases) page
2. Extract the archive:
   ```bash
   tar -xzf ProxyBridge-Linux-vX.X.X.tar.gz
   cd ProxyBridge-Linux-vX.X.X
   ```
3. Run the setup script with root privileges:
   ```bash
   sudo ./setup.sh
   ```

The setup script will:
- Install runtime dependencies (libnetfilter-queue, iptables, GTK3)
- Copy binaries to `/usr/local/bin` (ProxyBridge CLI and GUI)
- Copy library to `/usr/local/lib` (libproxybridge.so)
- Update library cache
- Verify installation

**What gets installed:**
- `/usr/local/bin/ProxyBridge` - Command-line interface
- `/usr/local/bin/ProxyBridgeGUI` - Graphical interface (if GTK3 available)
- `/usr/local/lib/libproxybridge.so` - Core library
- `/etc/proxybridge/` - Configuration directory

## Usage

### GUI Application

**Launch GUI (requires GTK3):**

```bash
sudo ProxyBridgeGUI
```

The GTK3-based GUI provides:
- Proxy configuration (SOCKS5/HTTP with authentication)
- Visual rule management with process selection
- Real-time connection monitoring
- Import/Export rules (JSON format compatible with Windows/macOS)
- DNS via Proxy toggle


### Command Line Interface (CLI)

The CLI provides powerful automation and scripting capabilities with rule-based traffic control.

#### Basic Usage

```bash
# Help menu
ProxyBridge --help

# Route curl through SOCKS5 proxy
sudo ProxyBridge --proxy socks5://127.0.0.1:1080 --rule "curl:*:*:TCP:PROXY"

# Route multiple processes in single rule (semicolon-separated)
sudo ProxyBridge --proxy http://127.0.0.1:8080 --rule "curl;wget;firefox:*:*:TCP:PROXY"

# Multiple rules with verbose connection logging
sudo ProxyBridge --proxy http://127.0.0.1:8080 \
    --rule "curl:*:*:TCP:PROXY" \
    --rule "wget:*:*:TCP:PROXY" \
    --verbose 2

# Block specific application from internet access
sudo ProxyBridge --rule "malware:*:*:BOTH:BLOCK"

# Route specific apps through proxy, block everything else
sudo ProxyBridge --proxy socks5://127.0.0.1:1080 \
    --rule "curl:*:*:TCP:PROXY" \
    --rule "firefox:*:*:TCP:PROXY" \
    --rule "*:*:*:BOTH:BLOCK"

# Route all through proxy except proxy app itself
sudo ProxyBridge --proxy socks5://127.0.0.1:1080 \
    --rule "*:*:*:TCP:PROXY" \
    --rule "burpsuite:*:*:TCP:DIRECT"

# Target specific IPs and ports
sudo ProxyBridge --proxy socks5://127.0.0.1:1080 \
    --rule "curl:192.168.*.*;10.10.*.*:80;443;8080:TCP:PROXY"

# IP range support
sudo ProxyBridge --proxy socks5://192.168.1.4:4444 \
    --rule "curl:3.19.110.0-3.19.115.255:*:TCP:PROXY"

# Cleanup after crash (removes iptables rules)
sudo ProxyBridge --cleanup
```

#### Command Line Options

```
sudo ProxyBridge --help

  ____                        ____       _     _            
 |  _ \ _ __ _____  ___   _  | __ ) _ __(_) __| | __ _  ___ 
 | |_) | '__/ _ \ \/ / | | | |  _ \| '__| |/ _` |/ _` |/ _ \
 |  __/| | | (_) >  <| |_| | | |_) | |  | | (_| | (_| |  __/
 |_|   |_|  \___/_/\_\\__, | |____/|_|  |_|\__,_|\__, |\___|
                      |___/                      |___/  V3.2.0

  Universal proxy client for Linux applications

        Author: Sourav Kalal/InterceptSuite
        GitHub: https://github.com/InterceptSuite/ProxyBridge

USAGE:
  ProxyBridge [OPTIONS]

OPTIONS:
  --proxy <url>          Proxy server URL with optional authentication
                         Format: type://ip:port or type://ip:port:username:password
                         Examples: socks5://127.0.0.1:1080
                                   http://proxy.com:8080:myuser:mypass
                         Default: socks5://127.0.0.1:4444

  --rule <rule>          Traffic routing rule (can be specified multiple times)
                         Format: process:hosts:ports:protocol:action
                           process  - Process name(s): curl, cur*, *, or multiple separated by ;
                           hosts    - IP/host(s): *, google.com, 192.168.*.*, or multiple separated by ; or ,
                           ports    - Port(s): *, 443, 80;8080, 80-100, or multiple separated by ; or ,
                           protocol - TCP, UDP, or BOTH
                           action   - PROXY, DIRECT, or BLOCK
                         Examples:
                           curl:*:*:TCP:PROXY
                           curl;wget:*:*:TCP:PROXY
                           *:*:53:UDP:PROXY
                           firefox:*:80;443:TCP:DIRECT

  --dns-via-proxy <bool> Route DNS queries through proxy
                         Values: true, false, 1, 0
                         Default: true

  --verbose <level>      Logging verbosity level
                           0 - No logs (default)
                           1 - Show log messages only
                           2 - Show connection events only
                           3 - Show both logs and connections

  --cleanup              Cleanup resources (iptables, etc.) from crashed instance
                         Use if ProxyBridge crashed without proper cleanup

  --help, -h             Show this help message

EXAMPLES:
  # Basic usage with default proxy
  sudo ProxyBridge --rule curl:*:*:TCP:PROXY

  # Multiple rules with custom proxy
  sudo ProxyBridge --proxy socks5://192.168.1.10:1080 \
       --rule curl:*:*:TCP:PROXY \
       --rule wget:*:*:TCP:PROXY \
       --verbose 2

  # Route DNS through proxy with multiple apps
  sudo ProxyBridge --proxy socks5://127.0.0.1:1080 \
       --rule "curl;wget;firefox:*:*:BOTH:PROXY" \
       --dns-via-proxy true --verbose 3

NOTE:
  ProxyBridge requires root privileges to use nfqueue.
  Run with 'sudo' or as root user.

```

#### Rule Format

**Format:** `process:hosts:ports:protocol:action`

- **process** - Process name(s): `curl`, `curl;wget;firefox`, `fire*`, or `*`
- **hosts** - Target IP/hostname(s): `*`, `192.168.1.1`, `192.168.*.*`, `10.10.1.1-10.10.255.255`, or `192.168.1.1;10.10.10.10`
- **ports** - Target port(s): `*`, `443`, `80;443;8080`, `80-8000`, or `80;443;8000-9000`
- **protocol** - `TCP`, `UDP`, or `BOTH`
- **action** - `PROXY`, `DIRECT`, or `BLOCK`

**Examples:**
```bash
# Single process to proxy
--rule "curl:*:*:TCP:PROXY"

# Multiple processes in one rule
--rule "curl;wget;firefox:*:*:TCP:PROXY"

# Wildcard process names
--rule "fire*:*:*:TCP:PROXY"  # Matches firefox, firebird, etc.

# Target specific IPs and ports
--rule "curl:192.168.*;10.10.*.*:80;443;8080:TCP:PROXY"

# IP range matching
--rule "curl:3.19.110.0-3.19.115.255:*:TCP:PROXY"

# Allow direct connection (bypass proxy)
--rule "burpsuite:*:*:TCP:DIRECT"
```

**Notes:**
- Process names are case-sensitive on Linux
- Use `*` as the process name to set a default action for all traffic
- Press `Ctrl+C` to stop ProxyBridge
- On crash, use `--cleanup` flag to remove iptables rules

## Use Cases

- Redirect proxy-unaware applications (games, desktop apps) through InterceptSuite/Burp Suite for security testing
- Route specific applications through Tor, SOCKS5, or HTTP proxies
- Intercept and analyze traffic from applications that don't support proxy configuration
- Test application behavior under different network conditions
- Analyze protocols and communication patterns
- Penetration testing of Linux applications
- Network traffic monitoring and debugging

## Current Limitations

- **IPv4 only** (IPv6 not supported)
- **WSL (Windows Subsystem for Linux) NOT supported** - Neither WSL1 nor WSL2 work with ProxyBridge:
  - **WSL2**: Kernel does not support `nfnetlink_queue` module
    - Extension shows as "builtin" but is non-functional at runtime
    - NFQUEUE handle creation fails
  - **WSL1**: Uses a translation layer instead of a real Linux kernel
    - Does not support Netfilter/iptables properly
    - NFQUEUE is completely unavailable
  - **Works on:** Native Linux, VirtualBox VMs, VMware, cloud instances (AWS, GCP, Azure), bare-metal servers
  - **Does NOT work on:** WSL1, WSL2, Docker containers with limited capabilities
  - **Windows users:** Use the Windows version of ProxyBridge instead - it's specifically designed for Windows and works perfectly
- **Root privileges required** - NFQUEUE and iptables require root access
- **GUI requires GTK3** - Command-line interface works without GUI dependencies

## Things to Note

- **DNS Traffic Handling**: DNS traffic on TCP and UDP port 53 is handled separately from proxy rules. Even if you configure rules for port 53, they will be ignored. Instead, DNS routing is controlled by the `--dns-via-proxy` flag (enabled by default). When enabled, all DNS queries are routed through the proxy; when disabled, DNS queries use direct connection.


- **Automatic Direct Routing**: Certain IP addresses and ports automatically use direct connection regardless of proxy rules:
  - **Broadcast addresses** (255.255.255.255 and x.x.x.255) - Network broadcast
  - **Multicast addresses** (224.0.0.0 - 239.255.255.255) - Group communication
  - **APIPA addresses** (169.254.0.0/16) - Automatic Private IP Addressing (link-local)
  - **DHCP ports** (UDP 67, 68) - Dynamic Host Configuration Protocol

  These addresses and ports are used by system components, network discovery, and essential Linux services.

- **UDP Proxy Requirements**: UDP traffic only works when a SOCKS5 proxy is configured. If an HTTP proxy server is configured, ProxyBridge will ignore UDP proxy rules and route UDP traffic as direct connection instead. This limitation does not affect UDP rules with BLOCK or DIRECT actions.

  **Important UDP Considerations**:
  - Configuring a SOCKS5 proxy does not guarantee UDP will work. Most SOCKS5 proxies do not support UDP traffic, including SSH SOCKS5 proxies.
  - The SOCKS5 proxy must support UDP ASSOCIATE command. If ProxyBridge fails to establish a UDP association with the SOCKS5 proxy, packets will fail to connect.
  - Many UDP applications use HTTP/3 and DTLS protocols. Even if your SOCKS5 proxy supports UDP ASSOCIATE, ensure it can handle DTLS and HTTP/3 UDP traffic, as they require separate handling beyond raw UDP packets.
  - **Testing UDP/HTTP3/DTLS Support**: If you need to test UDP, HTTP/3, and DTLS support with a SOCKS5 proxy, try [Nexus Proxy](https://github.com/InterceptSuite/nexus-proxy) - a proxy application created specifically to test ProxyBridge with advanced UDP protocols.

- **Root Privileges**: ProxyBridge requires root access to:
  - Create NFQUEUE handles for packet interception
  - Add/remove iptables rules in mangle and nat tables
  - Listen on relay ports (34010 for TCP, 34011 for UDP)

  Always run with `sudo` or as root user.

- **Process Name Matching**: Linux process names are case-sensitive. Use exact process names or wildcard patterns:
  - Exact: `firefox` matches only "firefox"
  - Wildcard: `fire*` matches "firefox", "firebird", "firestorm", etc.
  - All: `*` matches all processes

## How It Works

ProxyBridge uses Linux Netfilter NFQUEUE to intercept TCP/UDP packets and applies user-defined rules to route traffic through proxies.

**Case 1: Packet does not match any rules**

```
Application → TCP/UDP Packet → NFQUEUE → ProxyBridge
                                            ↓
                                     [No Match or DIRECT]
                                            ↓
                                      Packet Verdict: ACCEPT
                                            ↓
                                    Direct Connection → Internet
```

**Case 2: Packet matches proxy rule**

```
Application → TCP/UDP Packet → NFQUEUE → ProxyBridge
                                            ↓
                                        [PROXY Rule Match]
                                            ↓
                                      Packet Verdict: ACCEPT + Mark
                                            ↓
                                    iptables NAT REDIRECT
                                            ↓
                            Relay Server (34010/34011) ← Packet
                                            ↓
                                [Store Original Destination]
                                            ↓
                          SOCKS5/HTTP Protocol Conversion
                                            ↓
                            Proxy Server (Burp Suite/InterceptSuite)
                                            ↓
                                Forward to Original Destination
                                            ↓
                                        Internet
                                            ↓
                                    Response Returns
                                            ↓
                                    Relay Server
                                            ↓
                        [Restore Original Source IP/Port]
                                            ↓
                                    Application Receives Response
```

**Detailed Traffic Flow:**

1. **Applications Generate Traffic** - User-mode applications (curl, wget, firefox, games) create TCP/UDP packets

2. **Kernel Interception** - iptables rules in the mangle table send packets to NFQUEUE:
   ```bash
   iptables -t mangle -A OUTPUT -p tcp -j NFQUEUE --queue-num 0
   iptables -t mangle -A OUTPUT -p udp -j NFQUEUE --queue-num 0
   ```

3. **NFQUEUE Delivery** - libnetfilter_queue delivers packets to ProxyBridge in userspace

4. **Rule Evaluation** - ProxyBridge inspects each packet and applies configured rules:
   - **BLOCK** → Packet verdict: DROP (no network access)
   - **DIRECT** → Packet verdict: ACCEPT (direct connection)
   - **NO MATCH** → Packet verdict: ACCEPT (direct connection)
   - **PROXY** → Packet verdict: ACCEPT + set mark (1 for TCP, 2 for UDP)

5. **NAT Redirection** - For PROXY-matched packets, iptables NAT rules redirect marked packets:
   ```bash
   iptables -t nat -A OUTPUT -p tcp -m mark --mark 1 -j REDIRECT --to-port 34010
   iptables -t nat -A OUTPUT -p udp -m mark --mark 2 -j REDIRECT --to-port 34011
   ```

6. **Relay Servers** - Local relay servers (34010 for TCP, 34011 for UDP):
   - Intercept redirected packets using getsockopt(SO_ORIGINAL_DST)
   - Store original destination IP and port
   - Convert raw TCP/UDP to SOCKS5/HTTP proxy protocol
   - Perform proxy authentication if configured
   - Forward to configured proxy server

7. **Proxy Forwarding** - Proxy server (Burp Suite/InterceptSuite) forwards traffic to the original destination

8. **Response Handling** - Return traffic flows back through relay servers, which restore original source IP/port

**Key Technical Points:**

- **NFQUEUE vs WinDivert**: Linux uses Netfilter NFQUEUE (kernel feature) instead of WinDivert (Windows kernel driver)

- **Why NFQUEUE Instead of eBPF**: While eBPF is the modern approach for Linux networking tasks and offers better performance, ProxyBridge uses NFQUEUE due to fundamental limitations discovered during development:
  - **Original Plan**: ProxyBridge 3.1.0 for Linux was initially developed using eBPF after weeks of implementation
  - **eBPF Memory Limitations**: eBPF provides limited memory space, which proved insufficient for ProxyBridge's feature set:
    - ProxyBridge supports multiple complex proxy rules with wildcard matching, IP ranges, and process patterns
    - Storing and evaluating these rules within eBPF's memory constraints was not feasible
    - Alternative workarounds added excessive latency (200-500ms+ per packet)
  - **Performance Requirements**: ProxyBridge's core design goals couldn't be met with eBPF:
    - Work efficiently with minimal memory usage under high load
    - Handle high traffic volumes (10,000+ concurrent connections)
    - **Network speed impact must be ≤2-5% for proxied traffic only**
    - **Zero performance impact on direct (non-proxied) traffic**
    - eBPF implementation with all required features caused 15-30% slowdown on all traffic
  - **NFQUEUE Advantages**:
    - Userspace processing allows unlimited memory for complex rule evaluation
    - Selective packet inspection - only examines packets, doesn't slow down uninspected traffic
    - Mature, stable kernel interface available on all Linux distributions
    - Lower latency than eBPF workarounds when handling complex rule sets

  **Verdict**: NFQUEUE provides the right balance of flexibility, performance, and compatibility for ProxyBridge's requirements. While eBPF excels for simple packet filtering, ProxyBridge's advanced rule engine and protocol conversion needs are better served by NFQUEUE's userspace processing model.

- **Packet Marking**: ProxyBridge marks packets (mark=1 for TCP, mark=2 for UDP) instead of modifying destination
- **iptables Integration**: Uses mangle table (pre-routing processing) + nat table (port redirection)
- **Transparent Redirection**: Applications remain completely unaware of proxying
- **SO_ORIGINAL_DST**: Socket option retrieves original destination after NAT redirect
- **Multi-threaded**: Separate threads for packet processing, TCP relay, UDP relay, and connection cleanup

**Architecture Advantages:**

- No kernel modules required (uses built-in Netfilter)
- Works on any Linux distribution with iptables and NFQUEUE support
- Leverages proven kernel infrastructure (Netfilter/iptables)
- Separate packet marking prevents packet modification in NFQUEUE
- Clean separation: NFQUEUE for inspection, iptables for redirection

## Build from Source

### Requirements

- Linux kernel with NFQUEUE support (virtually all modern distributions)
- GCC compiler
- Make
- Development libraries:
  - libnetfilter-queue-dev (Debian/Ubuntu) or libnetfilter_queue-devel (Fedora/RHEL)
  - libnfnetlink-dev (Debian/Ubuntu) or libnfnetlink-devel (Fedora/RHEL)
  - libgtk-3-dev (optional, for GUI) or gtk3-devel (Fedora/RHEL)
  - pkg-config

### Building

1. **Install build dependencies:**

   **Debian/Ubuntu/Mint:**
   ```bash
   sudo apt-get update
   sudo apt-get install build-essential gcc make \
       libnetfilter-queue-dev libnfnetlink-dev \
       libgtk-3-dev pkg-config
   ```

   **Fedora:**
   ```bash
   sudo dnf install gcc make \
       libnetfilter_queue-devel libnfnetlink-devel \
       gtk3-devel pkg-config
   ```

   **Arch/Manjaro:**
   ```bash
   sudo pacman -S base-devel gcc make \
       libnetfilter_queue libnfnetlink \
       gtk3 pkg-config
   ```

2. **Clone or download ProxyBridge source code:**
   ```bash
   git clone https://github.com/InterceptSuite/ProxyBridge.git
   cd ProxyBridge/Linux
   ```

3. **Build using the build script:**
   ```bash
   chmod +x build.sh
   ./build.sh
   ```

   This will compile:
   - `libproxybridge.so` - Core library
   - `ProxyBridge` - CLI application
   - `ProxyBridgeGUI` - GUI application (if GTK3 is available)

4. **Install to system paths:**
   ```bash
   sudo ./setup.sh
   ```

   Or manually copy binaries:
   ```bash
   sudo cp output/libproxybridge.so /usr/local/lib/
   sudo cp output/ProxyBridge /usr/local/bin/
   sudo cp output/ProxyBridgeGUI /usr/local/bin/  # If GUI was built
   sudo ldconfig
   ```

### Build Output

After successful build, binaries will be in the `output/` directory:
- `output/libproxybridge.so` - Shared library
- `output/ProxyBridge` - CLI binary
- `output/ProxyBridgeGUI` - GUI binary (if GTK3 available)

### Manual Compilation

**Library:**
```bash
cd src
gcc -Wall -Wextra -O3 -fPIC -D_GNU_SOURCE \
    -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE \
    -Wformat -Wformat-security -Werror=format-security \
    -fno-strict-overflow -fno-delete-null-pointer-checks -fwrapv \
    -c ProxyBridge.c -o ProxyBridge.o

gcc -shared -Wl,-z,relro,-z,now -Wl,-z,noexecstack -s \
    -o libproxybridge.so ProxyBridge.o \
    -lpthread -lnetfilter_queue -lnfnetlink
```

**CLI:**
```bash
cd cli
gcc -Wall -Wextra -O3 -I../src \
    -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE \
    -Wformat -Wformat-security -Werror=format-security \
    -fno-strict-overflow -fno-delete-null-pointer-checks -fwrapv \
    -c main.c -o main.o

gcc -o ProxyBridge main.o \
    -L../src -pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack -s \
    -Wl,-rpath,/usr/local/lib \
    -lproxybridge -lpthread
```

## Uninstallation

To remove ProxyBridge from your system:

```bash
# Remove binaries
sudo rm -f /usr/local/bin/ProxyBridge
sudo rm -f /usr/local/bin/ProxyBridgeGUI

# Remove library
sudo rm -f /usr/local/lib/libproxybridge.so

# Remove configuration
sudo rm -rf /etc/proxybridge

# Update library cache
sudo ldconfig

# Remove ld.so.conf entry (if exists)
sudo rm -f /etc/ld.so.conf.d/proxybridge.conf
sudo ldconfig
```

**Cleanup after crash:**

If ProxyBridge crashed and left iptables rules:
```bash
sudo ProxyBridge --cleanup
```

Or manually remove iptables rules:
```bash
sudo iptables -t mangle -D OUTPUT -p tcp -j NFQUEUE --queue-num 0
sudo iptables -t mangle -D OUTPUT -p udp -j NFQUEUE --queue-num 0
sudo iptables -t nat -D OUTPUT -p tcp -m mark --mark 1 -j REDIRECT --to-port 34010
sudo iptables -t nat -D OUTPUT -p udp -m mark --mark 2 -j REDIRECT --to-port 34011
```

## License

MIT License - See LICENSE file for details

---

**Author:** Sourav Kalal / InterceptSuite
**GitHub:** https://github.com/InterceptSuite/ProxyBridge
**Documentation:** https://github.com/InterceptSuite/ProxyBridge/tree/master/Linux
