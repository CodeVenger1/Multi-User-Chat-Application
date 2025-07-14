#include "socketutil.h"

atomic<bool> nicknameAccepted(false);
atomic<bool> awaitingNicknameInput(false);
atomic<bool> roomJoined(false);
atomic<bool> awaitingRoomSelection(false);
atomic<bool> isTypingPromptActive(false);
atomic<bool> isExitingApplication(false);

mutex clientStateMutex;
condition_variable clientStateCv;

string currentRoomNumber = "";

static mutex cout_mutex;

void printIncomingMessage(const string& message) {
    lock_guard<mutex> lock(cout_mutex);

    if (isTypingPromptActive.load()) {
        cout << "\r" << string(120, ' ') << "\r";
        cout << message;
        cout << "> " << flush;
    } else {
        cout << message;
    }
}

void receiveMessages(SOCKET serverSocketFD) {
    char buffer[1024];

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(serverSocketFD, buffer, sizeof(buffer) - 1, 0);

        if (bytesReceived <= 0) {
            if (bytesReceived == 0) {
                printIncomingMessage("\nServer disconnected gracefully.\n");
            } else {
                printIncomingMessage("\nServer receive failed with error: " + to_string(WSAGetLastError()) + "\n");
            }
            isExitingApplication.store(true);
            nicknameAccepted.store(true);
            roomJoined.store(true);
            awaitingNicknameInput.store(false);
            awaitingRoomSelection.store(false);
            clientStateCv.notify_all();
            break;
        }

        buffer[bytesReceived] = '\0';
        string message = buffer;

        if (message.rfind("NICK_REQUIRED", 0) == 0) {
            printIncomingMessage("Server: Please enter your desired nickname: ");
            awaitingNicknameInput.store(true);
            clientStateCv.notify_all();
        } else if (message.rfind("NICK_REJECTED", 0) == 0) {
            printIncomingMessage("Server: " + message);
            printIncomingMessage(" Please try another nickname: ");
            awaitingNicknameInput.store(true);
            clientStateCv.notify_all();
        } else if (message.rfind("NICK_ACCEPTED", 0) == 0) {
            printIncomingMessage("Nickname accepted! Proceeding to room selection...\n");
            nicknameAccepted.store(true);
            awaitingNicknameInput.store(false);
            awaitingRoomSelection.store(true);
            clientStateCv.notify_all();
        } else if (message.rfind("ROOM_JOINED:", 0) == 0) {
            currentRoomNumber = message.substr(message.find(":") + 1);
            currentRoomNumber = currentRoomNumber.substr(0, currentRoomNumber.find("\n"));
            printIncomingMessage("Server: Successfully joined room number '" + currentRoomNumber + "'.\n");
            roomJoined.store(true);
            awaitingRoomSelection.store(false);
            clientStateCv.notify_all();
        } else if (message.rfind("ROOM_LEFT:", 0) == 0) {
            string leftRoomNum = message.substr(message.find(":") + 1);
            leftRoomNum = leftRoomNum.substr(0, leftRoomNum.find("\n"));
            printIncomingMessage("Server: You have left room number '" + leftRoomNum + "'.\n");
            roomJoined.store(false);
            awaitingRoomSelection.store(true);
            clientStateCv.notify_all();
        }
        else if (message.rfind("ERROR:", 0) == 0) {
            printIncomingMessage("Server Error: " + message);
            if (!roomJoined.load()) {
                awaitingRoomSelection.store(true);
                clientStateCv.notify_all();
            }
        } else if (message.rfind("USER_LIST:", 0) == 0) {
            size_t first_colon = message.find(":");
            size_t second_colon = message.find(":", first_colon + 1);
            if (first_colon != string::npos && second_colon != string::npos) {
                string roomNum = message.substr(first_colon + 1, second_colon - (first_colon + 1));
                string users = message.substr(second_colon + 1);
                users = users.substr(0, users.find("\n"));
                printIncomingMessage("--- Users in room number '" + roomNum + "': " + users + " ---\n");
            }
        } else {
            printIncomingMessage(message);
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

    while (!nicknameAccepted.load() && !isExitingApplication.load()) {
        unique_lock<mutex> lock(clientStateMutex);
        clientStateCv.wait(lock, []{ return awaitingNicknameInput.load() || nicknameAccepted.load() || isExitingApplication.load(); });

        if (isExitingApplication.load()) break;

        if (nicknameAccepted.load()) {
            break;
        }

        if (awaitingNicknameInput.load()) {
            isTypingPromptActive.store(true);
            getline(cin, input);
            isTypingPromptActive.store(false);

            string nickCommand = "NICK " + input + "\n";
            send(clientSocketFD, nickCommand.c_str(), nickCommand.length(), 0);
            awaitingNicknameInput.store(false);
        }
    }

    if (isExitingApplication.load()) {
        cerr << "Exiting application due to server disconnect or user choice." << endl;
        closesocket(clientSocketFD);
        WSACleanup();
        return 1;
    }

room_selection_start: // Label for goto
    while (!roomJoined.load() && !isExitingApplication.load()) {
        unique_lock<mutex> lock(clientStateMutex);
        clientStateCv.wait(lock, []{ return awaitingRoomSelection.load() || roomJoined.load() || isExitingApplication.load(); });

        if (isExitingApplication.load()) break;

        if (roomJoined.load()) {
            break;
        }

        if (awaitingRoomSelection.load()) {
            cout << "\nDo you want to (1) Create a new room, (2) Join an existing room, or (3) Exit Application? (Enter 1, 2, or 3): ";
            isTypingPromptActive.store(true);
            getline(cin, input);
            isTypingPromptActive.store(false);

            string roomNumberStr;
            if (input == "1") {
                cout << "Enter the number for your new room: ";
                isTypingPromptActive.store(true);
                getline(cin, roomNumberStr);
                isTypingPromptActive.store(false);
            } else if (input == "2") {
                cout << "Enter the number of the room you want to join: ";
                isTypingPromptActive.store(true);
                getline(cin, roomNumberStr);
                isTypingPromptActive.store(false);
            } else if (input == "3") {
                isExitingApplication.store(true);
                break;
            }
            else {
                cout << "Invalid choice. Please enter 1, 2, or 3." << endl;
                awaitingRoomSelection.store(true);
                continue;
            }

            if (roomNumberStr.empty() && !isExitingApplication.load()) {
                cout << "Room number cannot be empty. Please try again." << endl;
                awaitingRoomSelection.store(true);
                continue;
            }

            if (!isExitingApplication.load()) {
                string joinCommand = "COMMAND:JOIN:" + roomNumberStr + "\n";
                send(clientSocketFD, joinCommand.c_str(), joinCommand.length(), 0);
                awaitingRoomSelection.store(false);
            }
        }
    }

    if (isExitingApplication.load()) {
        cerr << "Exiting application." << endl;
        WSACleanup();
        return 0;
    }

    printIncomingMessage("You are in room number '" + currentRoomNumber + "'. Start typing your messages (type 'exit' or 'quit' to leave):\n");
    isTypingPromptActive.store(true);
    cout << "> " << flush;

    while (getline(cin, input)) {
        if (input == "exit" || input == "quit") {
            string leaveCommand = "COMMAND:LEAVE\n";
            send(clientSocketFD, leaveCommand.c_str(), leaveCommand.length(), 0);

            roomJoined.store(false);
            awaitingRoomSelection.store(true);
            isTypingPromptActive.store(false);

            cout << "\nLeaving room... returning to room selection.\n";
            break;
        }

        if (input.empty()) {
            cout << "> " << flush;
            continue;
        }

        input += "\n";

        int send_status = send(clientSocketFD, input.c_str(), input.length(), 0);
        if (send_status == SOCKET_ERROR) {
            cerr << "send failed with error: " << WSAGetLastError() << endl;
            break;
        }
        cout << "> " << flush;
    }

    if (!isExitingApplication.load()) {
        goto room_selection_start;
    }

    isTypingPromptActive.store(false);
    shutdown(clientSocketFD, SD_SEND);
    closesocket(clientSocketFD);
    WSACleanup();
    return 0;
}