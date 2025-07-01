#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <windows.h>
#include <atomic>
#include <string>
#include <mstcpip.h>
#include <conio.h>
using namespace std;

std::atomic<bool> running{true};
string username;
string ipAddress;
int ipPort;
SOCKET server = INVALID_SOCKET;


void cleanup() {
    if (server != INVALID_SOCKET) {
        string exit_message = username + " has left the chat";
        send(server, exit_message.c_str(), static_cast<int>(exit_message.length()), 0);

        
        shutdown(server, SD_BOTH);
        closesocket(server);
        server = INVALID_SOCKET;
    }

    WSACleanup();
}


BOOL WINAPI ConsoleHandler(DWORD signal) {
    switch (signal) {
        case CTRL_CLOSE_EVENT:   
        case CTRL_SHUTDOWN_EVENT: 
        case CTRL_LOGOFF_EVENT:  
        case CTRL_BREAK_EVENT:   
        case CTRL_C_EVENT:       
            running = false;
            cleanup();
            return TRUE;
        default: ;
    }
    return FALSE;
}


constexpr int KEEPALIVE_TIME = 10000;    
constexpr int KEEPALIVE_INTERVAL = 1000;  
constexpr int CONNECT_TIMEOUT = 5000;     
constexpr int MAX_RECONNECT_ATTEMPTS = 3;

void clearBottomTwo(const string& prefix, const string& message) {
    std::cout << "\x1b[2F";
    std::cout << "\x1b[2K";
    if (prefix == "You: ") {
        cout << "\x1b[32m" << prefix << message << "\x1b[0m" << endl;  
    } else {
        cout << prefix << message << endl;  
    }

    std::cout << "\x1b[2K";
    std::cout << "\x1b[0G";
}

void printInputSection() {
    cout << "-------------------------" << "\n";
    cout << "Enter message: " << flush;
}

bool configureSocket(SOCKET sock) {
    
    BOOL nodelay = TRUE;
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&nodelay), sizeof(nodelay)) != 0) {
        cout << "Failed to set TCP_NODELAY" << endl;
        return false;
    }

    
    BOOL keepalive = TRUE;
    if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<char *>(&keepalive), sizeof(keepalive)) != 0) {
        cout << "Failed to set SO_KEEPALIVE" << endl;
        return false;
    }

    
    tcp_keepalive ka{};
    ka.onoff = 1;
    ka.keepalivetime = KEEPALIVE_TIME;
    ka.keepaliveinterval = KEEPALIVE_INTERVAL;

    DWORD bytes_returned;
    if (WSAIoctl(sock, SIO_KEEPALIVE_VALS, &ka, sizeof(ka),
                 nullptr, 0, &bytes_returned, nullptr, nullptr) == SOCKET_ERROR) {
        cout << "Failed to set keep-alive parameters" << endl;
        return false;
    }

    
    u_long mode = 1;
    if (ioctlsocket(sock, FIONBIO, &mode) != 0) {
        cout << "Failed to set non-blocking mode" << endl;
        return false;
    }

    return true;
}

bool connectWithTimeout(SOCKET sock, const sockaddr* addr, int addrlen, int timeout_ms) {
    
    int result = connect(sock, addr, addrlen);
    if (result == 0) return true;

    if (WSAGetLastError() != WSAEWOULDBLOCK) return false;

    
    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(sock, &write_set);

    timeval timeout = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    result = select(0, nullptr, &write_set, nullptr, &timeout);

    if (result <= 0) return false;

    
    int error = 0;
    int len = sizeof(error);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &len) != 0) return false;

    
    u_long mode = 0;
    ioctlsocket(sock, FIONBIO, &mode);

    return (error == 0);
}

void receive_messages(SOCKET server, const string& input) {
    char buffer[4096];
    while (running) {
        int bytes_received = recv(server, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            if (running) {
                clearBottomTwo("", "Server disconnected or error occurred.");
                running = false;
            }
            break;
        }

        buffer[bytes_received] = '\0';
        string message(buffer);

        string self_prefix = username + ": ";
        if (message.find(self_prefix) == 0) continue;

        cout << "\n";
        clearBottomTwo("", message);
        printInputSection();
        cout << input;
        cout.flush();
    }
}

SOCKET createAndConnectSocket(const string& ip, int port) {
    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        cout << "Failed to create socket" << endl;
        return INVALID_SOCKET;
    }

    if (!configureSocket(server)) {
        closesocket(server);
        return INVALID_SOCKET;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        cout << "Invalid IP address" << endl;
        closesocket(server);
        return INVALID_SOCKET;
    }

    cout << "Attempting to connect to server..." << endl;

    if (!connectWithTimeout(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr), CONNECT_TIMEOUT)) {
        cout << "Connection attempt timed out or failed" << endl;
        closesocket(server);
        return INVALID_SOCKET;
    }

    return server;
}

int main(int argc, char *argv[]) {

    
    if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
        cout << "Could not set control handler" << endl;
        return 1;
    }

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        cout << "Failed to initialize WinSock: " << WSAGetLastError() << endl;
        return 1;
    }
    cout << "Enter Username: ";
    getline(cin, username);

    cout << "Enter server IP: ";
    getline(cin, ipAddress);

    cout << "Enter server Port: ";
    cin >> ipPort;

    server = createAndConnectSocket(ipAddress, ipPort);
    int reconnect_attempts = 0;

    while (reconnect_attempts < MAX_RECONNECT_ATTEMPTS && server == INVALID_SOCKET) {
        if (reconnect_attempts > 0) {
            cout << "Reconnection attempt " << reconnect_attempts << " of " << MAX_RECONNECT_ATTEMPTS << endl;
            Sleep(1000);
        }
        server = createAndConnectSocket(ipAddress, ipPort);
        reconnect_attempts++;
    }

    if (server == INVALID_SOCKET) {
        cout << "Failed to connect after " << MAX_RECONNECT_ATTEMPTS << " attempts" << endl;
        WSACleanup();
        return 1;
    }

    cout << "Connected to server!" << endl;

    
    string join_message = username + " has joined the chat";
    if (send(server, join_message.c_str(), static_cast<int>(join_message.length()), 0) == SOCKET_ERROR) {
        cout << "Failed to send connection announcement: " << WSAGetLastError() << endl;
        cleanup();
        return 1;
    }
    string input;
    thread receive_thread(receive_messages, server, ref(input));

    printInputSection();
    char ch;
    while (running) {
        while ((ch = static_cast<char>(getch())) != '\r') {  
            if (ch == 8) {  
                if (!input.empty()) {
                    input.pop_back();
                    std::cout << "\b \b";  
                }
            } else if (ch != '\r') {  
                input += ch;
                std::cout << ch;  
            }
        }

        
        string message = input;
        if (!input.empty()) {
            std::cout << "\x1b[2K";  
            cout << "\n";
            clearBottomTwo("You: ", message);
        }
        input = "";

        if (!running) break; 



        if (message == "exit") {
            cout << "Exiting..." << endl;
            running = false;
            break;
        }

        if (message.empty()) {
            
            std::cout << "\x1b[1F";
            std::cout << "\x1b[0G";
            printInputSection();
            continue;
        }


        string buffer = username + ": " += message;
        int result = send(server, buffer.c_str(), static_cast<int>(buffer.length()), 0);
        if (result == SOCKET_ERROR) {
            int error = WSAGetLastError();
            cout << "Failed to send message. Error code: " << error << endl;
            if (error == WSAECONNRESET || error == WSAECONNABORTED) {
                cout << "Connection to server lost" << endl;
                running = false;
                break;
            }
        }

        printInputSection();
    }

    running = false;
    cleanup();

    if (receive_thread.joinable()) {
        receive_thread.join();
    }

    cout << "Client closed." << endl;
    return 0;
}