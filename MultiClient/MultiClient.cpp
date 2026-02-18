#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "../Common/Message.h"
#include "../Common/NetConstants.h"
#include "../Common/Protocol.h"

#pragma comment(lib,"Ws2_32.lib")
#define SERVER_IP "127.0.0.1"

bool recv_all(SOCKET s, char* buf, int len) { int total = 0; while (total < len) { int n = recv(s, buf + total, len - total, 0); if (n <= 0) return false; total += n; } return true; }
bool send_all(SOCKET s, const char* buf, int len) { int total = 0; while (total < len) { int n = send(s, buf + total, len - total, 0); if (n <= 0) return false; total += n; } return true; }
bool send_message(SOCKET s, int client_id, int type, const std::string& payload) { MessageHeader hdr{ client_id,type,(int)payload.size() }; send_all(s, (const char*)&hdr, sizeof(hdr)); if (hdr.payload_len > 0) send_all(s, payload.c_str(), hdr.payload_len); return true; }
bool recv_message(SOCKET s, MessageHeader& hdr, std::string& payload) { if (!recv_all(s, (char*)&hdr, sizeof(hdr))) return false; payload.clear(); if (hdr.payload_len > 0) { payload.resize(hdr.payload_len); if (!recv_all(s, &payload[0], hdr.payload_len)) return false; } return true; }

void run_client(int id) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server{}; server.sin_family = AF_INET; server.sin_port = htons(PORT); inet_pton(AF_INET, SERVER_IP, &server.sin_addr);
    if (connect(sock, (sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) { std::cout << "Client " << id << " failed to connect\n"; return; }

    int client_id = 0;
    std::string username = "client_" + std::to_string(id);
    send_message(sock, client_id, REGISTER, username);

    MessageHeader hdr; std::string payload;
    if (!recv_message(sock, hdr, payload)) { closesocket(sock); return; }
    client_id = hdr.client_id;
    std::cout << "[" << username << "] " << payload << "\n";

    // Igra automatski - random pokusaji dok ne pogodi ili ne ostane bez pokusaja
    int guess = 0;
    while (true) {
        guess = rand() % 100 + 1; // random guess 1-100
        send_message(sock, client_id, GUESS_NUMBER, std::to_string(guess));
        if (!recv_message(sock, hdr, payload)) break;
        if (hdr.request_type == SERVER_HIGHER || hdr.request_type == SERVER_LOWER) continue;
        else if (hdr.request_type == SERVER_WIN) { std::cout << "[" << username << "] POGODIO\n"; break; }
        else if (hdr.request_type == SERVER_LOSE) { std::cout << "[" << username << "] IZGUBIO. " << payload << "\n"; break; }
    }
    send_message(sock, client_id, DISCONNECT, "");
    closesocket(sock);
}

int main() {
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    int NUM_CLIENTS = 5; // broj klijenata
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_CLIENTS; i++) threads.emplace_back(run_client, i + 1);
    for (auto& t : threads) t.join();
    WSACleanup();
    return 0;
}
