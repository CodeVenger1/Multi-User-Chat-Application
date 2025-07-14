#include "socketutil.h"

using namespace std; // Added to remove std:: prefix

atomic<bool> nicknameAccepted(false);
atomic<bool> awaitingNicknameInput(false);

mutex nicknameStateMutex;
condition_variable nicknameStateCv;

void receiveMessages(SOCKET serverSocketFD) {
    char buffer[1024];

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(serverSocketFD, buffer, sizeof(buffer) - 1, 0);

        if (bytesReceived <= 0) {
            if (bytesReceived == 0) {
                cout << "\nServer disconnected gracefully." << endl;
            } else {
                cerr << "\nServer receive failed with error: " << WSAGetLastError() << endl;
            }
            nicknameAccepted.store(true);
            awaitingNicknameInput.store(false);
            nicknameStateCv.notify_all();
            break;
        }

        buffer[bytesReceived] = '\0';
        string message = buffer;

        if (message.rfind("NICK_REQUIRED", 0) == 0) {
            cout << "Server: Please enter your desired nickname: ";
            awaitingNicknameInput.store(true);
            nicknameStateCv.notify_all();
        } else if (message.rfind("NICK_REJECTED", 0) == 0) {
            cout << "Server: " << message;
            cout << " Please try another nickname: ";
            string nickname;
            getline(cin, nickname);
            string nickCommand = "NICK " + nickname + "\n";
            send(serverSocketFD, nickCommand.c_str(), nickCommand.length(), 0);
        } else if (message.rfind("NICK_ACCEPTED", 0) == 0) {
            cout << "Nickname accepted! You can now chat.\n" << endl;
            nicknameAccepted.store(true);
            awaitingNicknameInput.store(false);
            nicknameStateCv.notify_all();
        } else {
            cout << message;
        }
    }
    shutdown(serverSocketFD, SD_SEND);
    closesocket(serverSocketFD);
}

int main() {
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        cerr << "WSAStartup failed: " << iResult << endl;
        return 1;
    }

    SOCKET clientSocketFD = CreateTCPIPv4Socket();
    if (clientSocketFD == INVALID_SOCKET) {
        cerr << "socket failed with error: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in address = CreateIPv4Address("127.0.0.1", 8580);

    int connection_status = connect(clientSocketFD, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (connection_status == SOCKET_ERROR) {
        cerr << "connect failed with error: " << WSAGetLastError() << endl;
        closesocket(clientSocketFD);
        WSACleanup();
        return 1;
    }
    cout << "Connected to server. Waiting for nickname prompt...\n" << endl;

    thread receiverThread(receiveMessages, clientSocketFD);
    receiverThread.detach();

    string input;
    while (!nicknameAccepted.load()) {
        unique_lock<mutex> lock(nicknameStateMutex);
        nicknameStateCv.wait(lock, []{ return awaitingNicknameInput.load() || nicknameAccepted.load(); });

        if (nicknameAccepted.load()) {
            break;
        }

        if (awaitingNicknameInput.load()) {
            getline(cin, input);
            string nickCommand = "NICK " + input + "\n";
            send(clientSocketFD, nickCommand.c_str(), nickCommand.length(), 0);
            awaitingNicknameInput.store(false);
        }
    }

    if (!nicknameAccepted.load()) {
        cerr << "Nickname negotiation failed or server disconnected. Exiting." << endl;
        WSACleanup();
        return 1;
    }

    cout << "Start typing your messages (type 'exit' or 'quit' to leave):\n";
    while (getline(cin, input)) {
        if (input == "exit" || input == "quit") {
            break;
        }

        if (input.empty()) {
            continue;
        }

        input += "\n";

        int send_status = send(clientSocketFD, input.c_str(), input.length(), 0);
        if (send_status == SOCKET_ERROR) {
            cerr << "send failed with error: " << WSAGetLastError() << endl;
            break;
        }
    }

    shutdown(clientSocketFD, SD_SEND);
    closesocket(clientSocketFD);
    WSACleanup();
    return 0;
}
