#include <iostream> //Standard C Libraries
#include <cstring>
#include <sys/socket.h> //Sockets
#include <netinet/in.h> //IP Domain Sockets
#include <unistd.h> //OS Level Functions

int main() {
    //Creating a socket
    //AF_INET = use IPv4, SOCK_STREAM = TCP
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    //socket() returns -1 on failure
    if (server_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }
    //debugging
    std::cout << "Socket created, fd = " << server_fd << "\n";

    //Describing the address/port we want to bind to
    sockaddr_in address{}; //struct for IPv4 addresses
    address.sin_family = AF_INET; //IPv4
    address.sin_addr.s_addr = INADDR_ANY;  // listen on all local interfaces
    address.sin_port = htons(8080);        

    //Bind the socket to the port
    //Singature demands a pointer to the generic struct sockaddr type
     if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        //debugging
        std::cerr << "Bind failed\n";
        return 1;
    }
    //debugging
    std::cout << "Bound to port 8080\n";

    
    //Mark the socket as passive — ready to accept incoming connections
    //The second argument (5) is the "backlog": how many pending connections
    if (listen(server_fd, 5) < 0) {
        std::cerr << "Listen failed\n";
        return 1;
    }

    //debugging
    std::cout << "Listening on port 8080...\n";

    //Empty struct for the connecting client's IP and port
    sockaddr_in client_address{};
    //Size of the struct
    socklen_t client_len = sizeof(client_address);

    //Pauses executiion, brand new socket for the client.
    int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);

    if (client_fd < 0) {
        std::cerr << "Accept failed\n";
        return 1;
    }

    //debugging
    std::cout << "Client connected! fd = " << client_fd << "\n";

    //raw block of memory
    char buffer[1024];

    while (true) {
        //recv() reads incoming data sent by client on specific connection
        //buffer where to dump memory, max bytes 1024
        ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);

        //Client closed connection
        if (bytes_read <= 0) {
            std::cout << "Client disconnected\n";
            break;
        }

        std::cout << "Received " << bytes_read << " bytes: "
                   << std::string(buffer, bytes_read);

        // send() writes those same bytes straight back to the client
        send(client_fd, buffer, bytes_read, 0);
    }

    close(client_fd);
    close(server_fd);
    return 0;
}

