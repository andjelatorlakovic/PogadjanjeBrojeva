#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include "../Common/Protocol.h"
#include "../Common/NetConstants.h"
#include "../Common/Message.h"

#pragma comment(lib, "Ws2_32.lib")

#define SERVER_IP "127.0.0.1"

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

// ---------- GLOBALNE VARIJABLE ----------
SOCKET sock = INVALID_SOCKET;
int my_id = 0;
std::atomic<bool> game_running{ false };
std::atomic<bool> game_finished{ false };
std::atomic<bool> is_registered{ false };

std::mutex game_mtx;
std::condition_variable game_cv;
std::atomic<bool> server_replied{ false };

// ---------- FUNKCIJE ----------
void print_menu() {
    std::cout << "\n========== MENU ==========\n";
    if (!is_registered) std::cout << "1. Connect (register)\n";
    else {
        std::cout << "2. Start game\n";
        std::cout << "3. Disconnect\n";
    }
    std::cout << "0. Exit\n";
    std::cout << "==========================\n";
    std::cout << "Izbor: ";
}

void send_request(int type, const std::string& payload) {
    if (sock == INVALID_SOCKET) return;
    MessageHeader hdr{ my_id, type, (int)payload.size() };
    send(sock, (char*)&hdr, sizeof(hdr), 0);
    if (hdr.payload_len > 0) send(sock, payload.c_str(), hdr.payload_len, 0);
}

void recv_thread() {
    CircularBuffer tempBuf;
    char buf[BUF_SIZE];

    while (true) {
        if (sock == INVALID_SOCKET) break;
        int n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) {
            std::cout << "\n[SISTEM] Veza sa serverom prekinuta.\n";
            closesocket(sock);
            sock = INVALID_SOCKET;
            game_running = false;
            game_finished = true;
            is_registered = false;
            print_menu();
            break;
        }

        tempBuf.push(buf, n);

        while (tempBuf.available() >= (int)sizeof(MessageHeader)) {
            MessageHeader hdr;
            tempBuf.peek((char*)&hdr, sizeof(hdr));
            if (tempBuf.available() < sizeof(hdr) + hdr.payload_len) break;
            tempBuf.pop(nullptr, sizeof(hdr));

            std::string payload(hdr.payload_len, 0);
            if (hdr.payload_len > 0) tempBuf.pop(&payload[0], hdr.payload_len);

            if (hdr.client_id != 0) my_id = hdr.client_id;

            if (payload == "CONNECTED_OK") {
                is_registered = true;
                std::cout << "\n[SISTEM] Registrovan korisnik! Možete sada da pokrenete igru.\n";
                print_menu();
            }

            if (hdr.request_type == WIN || hdr.request_type == LOSE) {
                std::cout << "\n[KRAJ IGRE]: " << payload << std::endl;
                game_finished = true;
                game_running = false;
                print_menu();
            }
            else if (!payload.empty()) {
                std::cout << "\n[SERVER]: " << payload << std::endl;
            }

            server_replied = true;
            game_cv.notify_all();
        }
    }
}

void play_game() {
    game_running = true;
    game_finished = false;

    while (game_running && !game_finished) {
        std::string guess;
        std::cout << "Pogodi broj: ";
        std::getline(std::cin, guess);
        if (guess.empty()) continue;

        server_replied = false;
        send_request(GUESS, guess);

        std::unique_lock<std::mutex> lk(game_mtx);
        game_cv.wait(lk, [] { return server_replied.load(); });
    }
}

// ---------- MENU ----------
void menu() {
    int choice;
    std::string input;

    print_menu();

    while (true) {
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
            continue;
        }
        std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');

        if (choice == 0) break;

        if (!is_registered) {
            if (choice == 1) {
                if (sock == INVALID_SOCKET) {
                  
                    sock = socket(AF_INET, SOCK_STREAM, 0);
                    if (sock == INVALID_SOCKET) {
                        std::cerr << "Socket nije kreiran!\n";
                        continue;
                    }

                    sockaddr_in addr{};
                    addr.sin_family = AF_INET;
                    addr.sin_port = htons(PORT);
                    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

                    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
                        std::cerr << "Greska pri povezivanju sa serverom.\n";
                        closesocket(sock);
                        sock = INVALID_SOCKET;
                        continue;
                    }

                    std::cout << "Povezani ste na server (" << SERVER_IP << ").\n";

                    std::thread t_recv(recv_thread);
                    t_recv.detach();
                }

                std::cout << "Unesi username: ";
                std::getline(std::cin, input);
                send_request(CONNECT, input);
            }
            else {
                std::cout << "Morate se prvo registrovati!\n";
                print_menu();
            }
        }
        else {
            switch (choice) {
            case 2:
                send_request(START_GAME, "");
                play_game();
                break;
            case 3:
                send_request(DISCONNECT, "");
                is_registered = false;
                game_running = false;
                game_finished = false;
                std::cout << "Odjavljeni ste.\n";
                print_menu();
                break;
            default:
                std::cout << "Nepoznata opcija.\n";
                print_menu();
            }
        }
    }
}

// ---------- MAIN ----------
int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    // Kreiraj socket odmah u mainu
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket nije kreiran u main!\n";
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Greska pri povezivanju sa serverom u main.\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::cout << "Povezani ste na server (" << SERVER_IP << ").\n";

    std::thread t_recv(recv_thread);
    t_recv.detach();

    menu();

    if (sock != INVALID_SOCKET) closesocket(sock);
    WSACleanup();
    return 0;
}
