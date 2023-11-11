#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <limits>

#define SERVER_IP "127.0.0.1"
#define PORT1 9001
#define PORT2 9002
#define BUFFER_SIZE 1024

int main(int argc, char* argv[]) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};

    std::string server_ip = "127.0.0.1"; // Default IP
    int server_port = 9002;              // Default port

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            if (i + 1 < argc) {
                server_ip = argv[++i];
            } else {
                std::cerr << "-h option requires one argument." << std::endl;
                return 1;
            }
        } else if (strcmp(argv[i], "-p") == 0) {
            if (i + 1 < argc) {
                server_port = atoi(argv[++i]);
            } else {
                std::cerr << "-p option requires one argument." << std::endl;
                return 1;
            }
        }
    }

    std::cout << "Connecting to server at " << server_ip << ":" << server_port << std::endl;
    // Creating socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Socket creation error" << std::endl;
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);


    // Convert IP address to binary format
    if (inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address or address not supported" << std::endl;
        return -1;
    }

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection failed" << std::endl;
        return -1;
    }

    std::cout << "Connected to server at " << server_ip << ":" << server_port << std::endl;


    while (true) {
        std::cout << "Enter a command (or 'exit' to quit): ";
        memset(buffer, 0, BUFFER_SIZE);
        std::cin.getline(buffer, BUFFER_SIZE);

        if (std::cin.fail()) {
            std::cin.clear(); // Clear the failbit
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Discard the rest of the line
            std::cerr << "Input error, please try again." << std::endl;
            continue; // Skip this iteration
        }

        if (strcmp(buffer, "exit") == 0) {
            break;
        }
        else {
            std::cout << "Sending command to server: " << buffer << std::endl;
            ssize_t sent_bytes = send(sock, buffer, strlen(buffer), 0);
            if (sent_bytes < 0) {
                std::cerr << "Send failed: " << strerror(errno) << std::endl;
                break;
            }
        }
        memset(buffer, 0, BUFFER_SIZE);
        int valread = read(sock, buffer, BUFFER_SIZE - 1);
        if (valread == -1) {
            std::cerr << "Read failed: " << strerror(errno) << std::endl;
            break;
        }

        buffer[valread] = '\0';
        std::cout << "Server response: " << buffer << std::endl;
    }
    close(sock);
    return 0;
}

