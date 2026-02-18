#include <windows.h>
#include <iostream>
#include <string>

void start_client_process(int id) {
    STARTUPINFO si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    // Putanja do klijenta - koristi relativnu putanju ka Debug folderu
    // Ako si iskopirala Client.exe u folder Launchera, ostavi samo L"Client.exe"
    std::wstring command = L"Client.exe";

    if (CreateProcess(NULL, (LPWSTR)command.c_str(), NULL, NULL, FALSE,
        CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        std::wcout << L"Pokrenut igrac " << id << std::endl;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else {
        std::cerr << "Greska pri pokretanju Client.exe! (Error " << GetLastError() << ")\n";
    }
}

int main() {
    int count;
    std::cout << "--- IKP GUESS GAME LAUNCHER ---\n";
    std::cout << "Koliko igraca zeli da ucestvuje? ";
    std::cin >> count;

    for (int i = 1; i <= count; i++) {
        start_client_process(i);
        Sleep(300); // Kratka pauza da se prozori ne preklapaju previse
    }

    std::cout << "\nSvi klijenti su poslati na server. Proveri nove prozore!\n";
    std::cout << "Pritisni ENTER za kraj launchera.";
    std::cin.ignore();
    std::cin.get();
    return 0;
}