#include <iostream>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <mutex>
#include <random>
#include <chrono>
#include <unordered_map>

#include "../Common/NetConstants.h"
#include "../Common/Message.h"
#include "../Common/Protocol.h"

#pragma comment(lib, "Ws2_32.lib")

std::mutex clients_mutex;

// ---------- CIRCULAR BUFFER ----------
#pragma pack(push, 1)
struct CircularBuffer {
    char data[BUF_SIZE];
    int head = 0, tail = 0;
    bool isEmpty() const { return head == tail; }
    bool isFull() const { return ((head + 1) % BUF_SIZE) == tail; }
    int available() const { return (head >= tail) ? (head - tail) : (BUF_SIZE - tail + head); }

    int push(const char* src, int len) {
        int pushed = 0;
        for (int i = 0; i < len && !isFull(); i++) {
            data[head] = src[i];
            head = (head + 1) % BUF_SIZE;
            pushed++;
        }
        return pushed;
    }

    bool pop(char* dst, int len) {
        if (available() < len) return false;
        for (int i = 0; i < len; i++) {
            if (dst) dst[i] = data[tail];
            tail = (tail + 1) % BUF_SIZE;
        }
        return true;
    }

    bool peek(char* dst, int len) const {
        if (available() < len) return false;
        int t = tail;
        for (int i = 0; i < len; i++) {
            dst[i] = data[t];
            t = (t + 1) % BUF_SIZE;
        }
        return true;
    }
};
#pragma pack(pop)

// ---------- CLIENT STRUCT ----------
struct Client {
    int id = -1;
    SOCKET sock = INVALID_SOCKET;
    char username[32]{};
    bool game_active = false;
    int secret_number = 0;
    int attempts_left = 0;
    CircularBuffer buffer;
    bool active = true;
};

// ---------- HASHMAP ZA KLIJENTE ----------
std::unordered_map<SOCKET, Client> clients;

void add_client(const Client& c) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    clients[c.sock] = c;
}

void remove_client(SOCKET sock) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    auto it = clients.find(sock);
    if (it != clients.end()) {
        closesocket(it->second.sock);
        clients.erase(it);
    }
}

int next_id = 1;

// ---------- POMOCNE FUNKCIJE ----------
int get_random(int min, int max) {
    static std::mt19937 rng((unsigned int)std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

void send_msg(SOCKET s, int id, int type, const std::string& payload) {
    MessageHeader hdr{ id, type, (int)payload.size() };
    send(s, (char*)&hdr, sizeof(hdr), 0);
    if (hdr.payload_len > 0) send(s, payload.c_str(), hdr.payload_len, 0);
}

void handle_request(Client& client, const MessageHeader& hdr, const std::string& payload) {
    switch (hdr.request_type) {
    case CONNECT: {
        client.id = next_id++;
        strncpy_s(client.username, payload.c_str(), _TRUNCATE);

        std::cout << "[CONN] Klijent: " << client.username
            << " (ID: " << client.id << ") povezan.\n";

        send_msg(client.sock, client.id, INFO, "CONNECTED_OK");
        break;
    }

    case START_GAME: {
        client.secret_number = get_random(MIN_NUMBER, MAX_NUMBER);
        client.attempts_left = get_random(8, 12);
        client.game_active = true;

        std::cout << "[START] " << client.username
            << " igra. Broj: " << client.secret_number
            << " | Pokusaja: " << client.attempts_left << "\n";

        send_msg(client.sock, client.id, INFO,
            "Igra pocela! Imate " + std::to_string(client.attempts_left) + " pokusaja.");
        break;
    }

    case GUESS: {
        bool valid = !payload.empty();
        for (char c : payload) if (!isdigit(c)) valid = false;

        if (!valid) {
            send_msg(client.sock, client.id, INFO, "Pogresan unos, pokusaj ponovo");
            break;
        }

        int guess = std::stoi(payload);
        client.attempts_left--;

        if (guess == client.secret_number) {
            client.game_active = false;
            send_msg(client.sock, client.id, WIN,
                "POGODAK! Broj je bio " + std::to_string(client.secret_number));

            std::cout << "[WIN] " << client.username << " je pogodio broj!\n";
        }
        else if (client.attempts_left <= 0) {
            client.game_active = false;
            send_msg(client.sock, client.id, LOSE,
                "IZGUBILI STE. Broj je bio " + std::to_string(client.secret_number));

            std::cout << "[LOSE] " << client.username << " je potrosio pokusaje.\n";
        }
        else {
            std::string hint = (guess < client.secret_number) ? "VECE" : "MANJE";
            send_msg(client.sock, client.id, RESULT,
                hint + " | Preostalo pokusaja: " + std::to_string(client.attempts_left));
        }
        break;
    }

    case DISCONNECT: {
        std::cout << "[DISC] Klijent " << client.username << " se diskonektovao.\n";
        client.active = false;
        break;
    }

    default:
        send_msg(client.sock, client.id, INFO, "UNKNOWN_REQUEST");
        break;
    }
}

// ---------- HANDLER ZA KLIJENTA ----------
void client_handler(Client& client) {
    char buf[BUF_SIZE];

    while (client.active) {
        int n = recv(client.sock, buf, sizeof(buf), 0);
        if (n <= 0) break;

        client.buffer.push(buf, n);

        while (true) {
            MessageHeader hdr;
            std::string payload;
            bool got = false;

            if (client.buffer.available() >= (int)sizeof(hdr)) {
                client.buffer.peek((char*)&hdr, sizeof(hdr));
                if (client.buffer.available() >= (int)(sizeof(hdr) + hdr.payload_len)) {
                    client.buffer.pop(nullptr, sizeof(hdr));
                    payload.resize(hdr.payload_len);
                    if (hdr.payload_len > 0) client.buffer.pop(&payload[0], hdr.payload_len);
                    got = true;
                }
            }

            if (!got) break;
            handle_request(client, hdr, payload);
        }
    }

    remove_client(client.sock);
}

// ---------- MAIN SERVER ----------
int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{ AF_INET, htons(PORT), INADDR_ANY };

    bind(s, (sockaddr*)&addr, sizeof(addr));
    listen(s, SOMAXCONN);

    std::cout << "SERVER POKRENUT - Slusam na portu " << PORT << "...\n";

    while (true) {
        SOCKET c_fd = accept(s, 0, 0);
        if (c_fd != INVALID_SOCKET) {
            Client c;
            c.sock = c_fd;
            add_client(c);
            std::thread(client_handler, std::ref(clients[c_fd])).detach();
        }
    }

    return 0;
}
