#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <sstream>

// Standard socket headers (same as before)
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

// OpenSSL headers — new territory
#include <openssl/ssl.h>
#include <openssl/err.h>

// -------------------------------------------------------
// CONSTANTS
// -------------------------------------------------------
const int PROXY_PORT = 8080;
const std::string CERTS_DIR = "/Users/Arnav/Documents/VSCProjects/egress-firewall/certs";
const std::string GEN_CERT_SCRIPT = CERTS_DIR + "/gen_cert.sh";

// -------------------------------------------------------
// HELPER: print any queued OpenSSL errors (useful for debugging)
// -------------------------------------------------------
void print_ssl_errors(const std::string& context) {
    unsigned long err;
    while ((err = ERR_get_error()) != 0) {
        char buf[256];
        ERR_error_string_n(err, buf, sizeof(buf));
        std::cerr << "[SSL ERROR] " << context << ": " << buf << "\n";
    }
}

// -------------------------------------------------------
// HELPER: connect a plain TCP socket to host:port
// (same logic as proxy.cpp, just extracted into a function)
// -------------------------------------------------------
int tcp_connect(const std::string& host, const std::string& port) {
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0) {
        std::cerr << "DNS resolution failed for " << host << "\n";
        return -1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        std::cerr << "TCP connect failed to " << host << ":" << port << "\n";
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return fd;
}

// -------------------------------------------------------
// STEP 1: Read the HTTP CONNECT request from the client
// and extract the target hostname and port.
//
// A CONNECT request looks like:
//   CONNECT example.com:443 HTTP/1.1\r\n
//   Host: example.com:443\r\n
//   \r\n
//
// We only need the first line.
// -------------------------------------------------------
bool parse_connect(int client_fd, std::string& host, std::string& port) {
    // Read byte by byte until we see the end of the HTTP headers (\r\n\r\n)
    std::string request;
    char c;
    while (true) {
        ssize_t n = recv(client_fd, &c, 1, 0);
        if (n <= 0) return false;
        request += c;
        if (request.size() >= 4 &&
            request.substr(request.size() - 4) == "\r\n\r\n") break;
    }

    // Parse the first line: "CONNECT host:port HTTP/1.1"
    std::istringstream stream(request);
    std::string method, target, version;
    stream >> method >> target >> version;

    if (method != "CONNECT") {
        std::cerr << "Expected CONNECT, got: " << method << "\n";
        return false;
    }

    // Split "host:port" into two parts
    size_t colon = target.rfind(':');
    if (colon == std::string::npos) return false;

    host = target.substr(0, colon);
    port = target.substr(colon + 1);

    std::cout << "CONNECT request for: " << host << ":" << port << "\n";
    return true;
}

// -------------------------------------------------------
// STEP 2: Run gen_cert.sh to get a fake cert for this hostname.
// Returns the cert path and key path via output params.
// -------------------------------------------------------
bool generate_cert(const std::string& host,
                   std::string& cert_path,
                   std::string& key_path) {
    std::string cmd = GEN_CERT_SCRIPT + " " + host;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;

    char buf[512];
    std::string output;
    while (fgets(buf, sizeof(buf), pipe)) output += buf;
    pclose(pipe);

    // Script prints: "/path/to/host.crt /path/to/host.key"
    std::istringstream ss(output);
    ss >> cert_path >> key_path;

    return !cert_path.empty() && !key_path.empty();
}

// -------------------------------------------------------
// STEP 3a: Create an SSL context for talking TO the client.
// We load our fake cert so the client thinks we're the real destination.
// -------------------------------------------------------
SSL_CTX* create_server_ctx(const std::string& cert_path,
                            const std::string& key_path) {
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) { print_ssl_errors("create_server_ctx"); return nullptr; }

    if (SSL_CTX_use_certificate_file(ctx, cert_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
        print_ssl_errors("load cert"); SSL_CTX_free(ctx); return nullptr;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
        print_ssl_errors("load key"); SSL_CTX_free(ctx); return nullptr;
    }
    return ctx;
}

// -------------------------------------------------------
// STEP 3b: Create an SSL context for talking TO the destination.
// We verify the real server's cert normally (like a browser would).
// -------------------------------------------------------
SSL_CTX* create_client_ctx() {
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { print_ssl_errors("create_client_ctx"); return nullptr; }

    // Tell OpenSSL where to find trusted CA certs on this Mac
    SSL_CTX_set_default_verify_paths(ctx);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    return ctx;
}

// -------------------------------------------------------
// STEP 4: Relay bytes between two SSL connections.
// Same idea as relay() in proxy.cpp, but reading/writing
// through SSL_read/SSL_write instead of recv/send.
// -------------------------------------------------------
void ssl_relay(SSL* from, SSL* to) {
    char buffer[4096];
    while (true) {
        int n = SSL_read(from, buffer, sizeof(buffer));
        if (n <= 0) break;

        // THIS is where inspection will happen in a future milestone:
        // buffer contains decrypted plaintext bytes right here.
        std::cout << "[INTERCEPTED " << n << " bytes]\n";

        int written = SSL_write(to, buffer, n);
        if (written <= 0) break;
    }
}

// -------------------------------------------------------
// MAIN
// -------------------------------------------------------
int main() {
    // Initialize OpenSSL
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    // Set up the listening socket (same as before)
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PROXY_PORT);

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);
    std::cout << "TLS proxy listening on port " << PROXY_PORT << "...\n";

    // Accept one client connection
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    std::cout << "Client connected\n";

    // Step 1: parse CONNECT
    std::string host, port;
    if (!parse_connect(client_fd, host, port)) {
        close(client_fd); close(server_fd); return 1;
    }

    // Step 2: reply 200 to client — tunnel is "established"
    std::string response = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client_fd, response.c_str(), response.size(), 0);

    // Step 3: generate fake cert for this hostname
    std::string cert_path, key_path;
    if (!generate_cert(host, cert_path, key_path)) {
        std::cerr << "Failed to generate cert for " << host << "\n";
        close(client_fd); close(server_fd); return 1;
    }
    std::cout << "Generated cert: " << cert_path << "\n";

    // Step 4: TLS handshake with client (we act as the server)
    SSL_CTX* server_ctx = create_server_ctx(cert_path, key_path);
    SSL* client_ssl = SSL_new(server_ctx);
    SSL_set_fd(client_ssl, client_fd);
    if (SSL_accept(client_ssl) <= 0) {
        print_ssl_errors("SSL_accept"); return 1;
    }
    std::cout << "TLS handshake with client complete\n";

    // Step 5: connect to real destination and do TLS handshake
    int dest_fd = tcp_connect(host, port);
    SSL_CTX* client_ctx = create_client_ctx();
    SSL* dest_ssl = SSL_new(client_ctx);
    SSL_set_fd(dest_ssl, dest_fd);
    SSL_set_tlsext_host_name(dest_ssl, host.c_str()); // SNI — tells the server which cert to serve
    if (SSL_connect(dest_ssl) <= 0) {
        print_ssl_errors("SSL_connect"); return 1;
    }
    std::cout << "TLS handshake with destination complete\n";

    // Step 6: relay in both directions concurrently
    std::thread t1(ssl_relay, client_ssl, dest_ssl);
    std::thread t2(ssl_relay, dest_ssl, client_ssl);
    t1.join();
    t2.join();

    // Cleanup
    SSL_free(client_ssl);
    SSL_free(dest_ssl);
    SSL_CTX_free(server_ctx);
    SSL_CTX_free(client_ctx);
    close(client_fd);
    close(dest_fd);
    close(server_fd);
    return 0;
}