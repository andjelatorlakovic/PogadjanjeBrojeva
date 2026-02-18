#include <iostream>
#include <thread>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <chrono>
#include <string>
#include <random>

#include "../Common/NetConstants.h"
#include "../Common/Message.h"
#include "../Common/Protocol.h"

#pragma comment(lib, "Ws2_32.lib")

#define SERVER_IP "127.0.0.1"

std::atomic<bool> keep_clients_alive{ true };

// ===================== GENERIC CLIENT =====================
SOCKET connect_to_server() {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

void send_request(SOCKET s, int type, const std::string& payload) {
    MessageHeader hdr{ 0, type, (int)payload.size() };
    send(s, (char*)&hdr, sizeof(hdr), 0);
    if (hdr.payload_len > 0) send(s, payload.c_str(), hdr.payload_len, 0);
}

// ===================== TEST 1: Mass Connection =====================
std::atomic<int> connected_count1{ 0 }, ack_count1{ 0 }, failed_count1{ 0 };

void connection_client(int idx) {
    SOCKET s = connect_to_server();
    if (s == INVALID_SOCKET) { failed_count1++; return; }

    connected_count1++;
    send_request(s, CONNECT, "bot_" + std::to_string(idx));

    // čekamo ACK
    MessageHeader hdr;
    if (recv(s, (char*)&hdr, sizeof(hdr), 0) > 0) {
        if (hdr.payload_len > 0) {
            std::string payload(hdr.payload_len, '\0');
            recv(s, &payload[0], hdr.payload_len, 0);
        }
        ack_count1++;
    }

    while (keep_clients_alive) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    closesocket(s);
}

void run_connection_test(int clients) {
    //std::cout << "\n>>> SNAPSHOT 1 (Baseline) <<<\n";
    //std::cout << "Pritisni ENTER da pokrenes masovno povezivanje..." << std::endl;
    //std::cin.get();

    keep_clients_alive = true;
    std::vector<std::thread> threads;
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < clients; i++)
        threads.emplace_back(connection_client, i);

    //std::cout << "\n>>> SNAPSHOT 2 (Peak) <<<\n";
    //std::cout << "Pritisni ENTER da zavrsis test i ugasis klijente..." << std::endl;
    //std::cin.get();

    keep_clients_alive = false;
    for (auto& t : threads) t.join();
    auto end = std::chrono::steady_clock::now();

    std::cout << "\n====== TEST 1 RESULTS ======\n";
    std::cout << "Attempted: " << clients << "\n";
    std::cout << "Connected: " << connected_count1.load() << "\n";
    std::cout << "ACK received: " << ack_count1.load() << "\n";
    std::cout << "Failed: " << failed_count1.load() << "\n";
    std::cout << "Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

    //std::cout << "\n>>> SNAPSHOT 3 (Recovery) <<<\n";
    //std::cout << "Pritisni ENTER za sledeci test..." << std::endl;
    //std::cin.get();
}

// ===================== TEST 2: Message Flood =====================
std::atomic<int> connected_count2{ 0 }, sent_messages2{ 0 }, failed_count2{ 0 };

int get_random(int min, int max) {
    static std::mt19937 rng((unsigned int)std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

void flood_client(int idx, int messages) {
    SOCKET s = connect_to_server();
    if (s == INVALID_SOCKET) { failed_count2++; return; }

    connected_count2++;
    send_request(s, CONNECT, "flood_bot_" + std::to_string(idx));
    send_request(s, START_GAME, "");

    for (int i = 0; i < messages; i++) {
        send_request(s, GUESS, std::to_string(get_random(0, 100)));
        sent_messages2++;
    }

    while (keep_clients_alive) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    closesocket(s);
}

void run_flood_test(int clients, int messages) {
    //std::cout << "\n>>> SNAPSHOT 1 (Pre-flood) <<<\n";
    //std::cout << "Pritisni ENTER da pokrenes flood test..." << std::endl;
    //std::cin.get();

    keep_clients_alive = true;
    std::vector<std::thread> threads;
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < clients; i++)
        threads.emplace_back(flood_client, i, messages);

    //std::cout << "\n>>> SNAPSHOT 2 (Flood Peak) <<<\n";
    //std::cout << "Pritisni ENTER da zavrsis flood test..." << std::endl;
    //std::cin.get();

    keep_clients_alive = false;
    for (auto& t : threads) t.join();
    auto end = std::chrono::steady_clock::now();

    std::cout << "\n====== TEST 2 RESULTS ======\n";
    std::cout << "Clients: " << clients << "\n";
    std::cout << "Messages sent: " << sent_messages2.load() << "\n";
    std::cout << "Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

    //std::cout << "\n>>> SNAPSHOT 3 (Recovery) <<<\n";
    //std::cout << "Pritisni ENTER za kraj..." << std::endl;
    //std::cin.get();
}

// ===================== MAIN =====================
int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    std::cout << ">>> STARTING MASS CONNECTION TEST <<<" << std::endl;
    run_connection_test(100);

    std::cout << "\n>>> STARTING MESSAGE FLOOD TEST <<<" << std::endl;
    run_flood_test(50, 1000);

    WSACleanup();
    std::cout << "\nSvi stres testovi su zavrseni.\n";
    getchar();
    return 0;
}
