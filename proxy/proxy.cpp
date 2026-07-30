#include <iostream>
#include <cstring>
#include <thread> //Threading
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> //DNS Related functions
#include <unistd.h>

// Resolves a hostname and connects to it, returning a connected socket fd (or -1 on failure)
int connect_to_destination(const char* host, const char* port) {
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;        // IPv4
    hints.ai_socktype = SOCK_STREAM;  // TCP

    // getaddrinfo does DNS resolution: turns "example.com" into an actual IP address
    if (getaddrinfo(host, port, &hints, &res) != 0) {
        std::cerr << "DNS resolution failed for " << host << "\n";
        return -1;
    }

    int dest_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (dest_fd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    if (connect(dest_fd, res->ai_addr, res->ai_addrlen) < 0) {
        std::cerr << "Connect to destination failed\n";
        close(dest_fd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);  // done with this, connect() already used what it needed
    return dest_fd;
}

// Copies bytes from `from` to `to`, until `from` closes or errors
void relay(int from, int to) {
    char buffer[4096];
    while (true) {
        ssize_t bytes_read = recv(from, buffer, sizeof(buffer), 0);
        if (bytes_read <= 0) break;
        send(to, buffer, bytes_read, 0);
    }
    // Half-close: tells the other side "no more data is coming from me,"
    // without fully closing (the OTHER thread might still be relaying data back)
    shutdown(to, SHUT_WR);
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 5);
    std::cout << "Proxy listening on port 8080...\n";

    sockaddr_in client_address{};
    socklen_t client_len = sizeof(client_address);
    int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
    std::cout << "Client connected, fd = " << client_fd << "\n";

    // Hardcoded destination for now — real HTTP server, plain (unencrypted) port 80
    int dest_fd = connect_to_destination("example.com", "80");
    if (dest_fd < 0) {
        close(client_fd);
        close(server_fd);
        return 1;
    }
    std::cout << "Connected to destination, fd = " << dest_fd << "\n";

    // Two threads, each responsible for one direction of traffic
    std::thread t1(relay, client_fd, dest_fd);  // client -> destination
    std::thread t2(relay, dest_fd, client_fd);  // destination -> client

    t1.join();
    t2.join();

    close(client_fd);
    close(dest_fd);
    close(server_fd);
    return 0;
}