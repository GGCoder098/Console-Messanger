#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <thread>
#include <mutex>
#include <set>
#include <atomic>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

class Server {
private:
    static constexpr int BUFFER_SIZE = 4096;
    static constexpr int KEEPALIVE_INTERVAL = 30; 

    SOCKET listen_socket;
    std::mutex cout_mutex;
    std::mutex clients_mutex;
    std::set<SOCKET> clients;
    std::atomic<bool> running{true};

    void safe_cout(const std::string& message) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "[" << std::time(nullptr) << "] " << message << std::endl;
    }

    bool configure_socket(SOCKET sock) {
        
        BOOL keepalive = TRUE;
        if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char*)&keepalive, sizeof(keepalive)) != 0) {
            safe_cout("Failed to set keepalive");
            return false;
        }

        
        DWORD timeout = 5000; 
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) != 0) {
            safe_cout("Failed to set receive timeout");
            return false;
        }

        
        BOOL nodelay = TRUE;
        if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay)) != 0) {
            safe_cout("Failed to set TCP_NODELAY");
            return false;
        }

        return true;
    }

    void broadcast_message(const char* message, int message_length, SOCKET sender) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        std::vector<SOCKET> failed_clients;

        for (SOCKET client : clients) {
            if (client != sender) {
                int total_sent = 0;
                while (total_sent < message_length) {
                    int result = send(client, message + total_sent,
                                   message_length - total_sent, 0);
                    if (result == SOCKET_ERROR) {
                        failed_clients.push_back(client);
                        break;
                    }
                    total_sent += result;
                }
            }
        }

        
        for (SOCKET failed : failed_clients) {
            remove_client(failed);
        }
    }

    void remove_client(SOCKET client_socket) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(client_socket);
        shutdown(client_socket, SD_BOTH);
        closesocket(client_socket);
        safe_cout("Client disconnected. Active clients: " + std::to_string(clients.size()));
    }

    void handle_client(SOCKET client_socket) {
        char buffer[BUFFER_SIZE];
        char client_ip[INET_ADDRSTRLEN];
        sockaddr_in addr;
        int addr_len = sizeof(addr);

        if (getpeername(client_socket, (sockaddr*)&addr, &addr_len) == 0) {
            inet_ntop(AF_INET, &addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            safe_cout("New client connected from " + std::string(client_ip));
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients.insert(client_socket);
            safe_cout("Total active clients: " + std::to_string(clients.size()));
        }

        while (running) {
            int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

            if (bytes_received == SOCKET_ERROR) {
                int error = WSAGetLastError();
                if (error == WSAETIMEDOUT) {
                    
                    continue;
                }
                safe_cout("Receive error: " + std::to_string(error));
                break;
            }

            if (bytes_received == 0) {
                safe_cout("Client closed connection");
                break;
            }

            buffer[bytes_received] = '\0';

            if (strcmp(buffer, "exit") == 0) {
                safe_cout("Client requested disconnect");
                break;
            }

            safe_cout("Received from " + std::string(client_ip) + ": " + std::string(buffer));
            broadcast_message(buffer, bytes_received, client_socket);
        }

        remove_client(client_socket);
    }

public:
    Server() : listen_socket(INVALID_SOCKET) {}

    ~Server() {
        stop();
    }

    bool start(unsigned short port) {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            safe_cout("Failed to initialize Winsock");
            return false;
        }

        listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_socket == INVALID_SOCKET) {
            safe_cout("Failed to create socket: " + std::to_string(WSAGetLastError()));
            WSACleanup();
            return false;
        }

        if (!configure_socket(listen_socket)) {
            closesocket(listen_socket);
            WSACleanup();
            return false;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        
        BOOL reuse = TRUE;
        if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR,
                      (char*)&reuse, sizeof(reuse)) != 0) {
            safe_cout("Failed to set SO_REUSEADDR");
            closesocket(listen_socket);
            WSACleanup();
            return false;
        }

        if (bind(listen_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            safe_cout("Bind failed: " + std::to_string(WSAGetLastError()));
            closesocket(listen_socket);
            WSACleanup();
            return false;
        }

        if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
            safe_cout("Listen failed: " + std::to_string(WSAGetLastError()));
            closesocket(listen_socket);
            WSACleanup();
            return false;
        }

        safe_cout("Server started on port " + std::to_string(port));
        return true;
    }

    void run() {
        std::vector<std::thread> client_threads;

        while (running) {
            sockaddr_in client_addr;
            int client_addr_size = sizeof(client_addr);

            SOCKET client_socket = accept(listen_socket, (sockaddr*)&client_addr, &client_addr_size);

            if (!running) break;

            if (client_socket == INVALID_SOCKET) {
                safe_cout("Accept failed: " + std::to_string(WSAGetLastError()));
                continue;
            }

            if (!configure_socket(client_socket)) {
                closesocket(client_socket);
                continue;
            }

            client_threads.emplace_back(&Server::handle_client, this, client_socket);

            
            auto it = client_threads.begin();
            while (it != client_threads.end()) {
                if (it->joinable()) {
                    it->detach();
                    it = client_threads.erase(it);
                } else {
                    ++it;
                }
            }
        }

        
        for (auto& thread : client_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void stop() {
        running = false;

        
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (SOCKET client : clients) {
                shutdown(client, SD_BOTH);
                closesocket(client);
            }
            clients.clear();
        }

        if (listen_socket != INVALID_SOCKET) {
            closesocket(listen_socket);
            listen_socket = INVALID_SOCKET;
        }

        WSACleanup();
    }
};

int main() {
    Server server;
    unsigned short port;
    std::cout << "Enter server port: ";
    std::cin >> port;
    if (!port) {
        port = 1027;
    }
    std::cin.clear();
    std::cin.ignore();
    if (!server.start(port)) {
        return 1;
    }

    
    std::thread server_thread([&server]() { server.run(); });

    std::cout << "Press Enter to stop the server..." << std::endl;
    std::cin.get();

    server.stop();
    server_thread.join();

    return 0;
}