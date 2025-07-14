#include "socketutil.h"

struct ClientState {
    string nickname;
    int currentRoomNumber;
};

map<SOCKET, ClientState> clientStates;
mutex clientStatesMutex;

struct AcceptedSocket {
    SOCKET acceptedSocketFD;
    sockaddr_in address;
    int errorCode;
    bool accepted;
};

string trim(const string& str) {
    size_t first = str.find_first_not_of(" \n\r\t");
    if (string::npos == first) {
        return "";
    }
    size_t last = str.find_last_not_of(" \n\r\t");
    return str.substr(first, (last - first + 1));
}

AcceptedSocket AcceptIncomeingConnection(SOCKET serverSocketFD) {
    AcceptedSocket result;
    result.accepted = false;
    result.errorCode = 0;

    sockaddr_in clientAddress;
    int clientAddressSize = sizeof(clientAddress);
    result.acceptedSocketFD = accept(serverSocketFD, reinterpret_cast<sockaddr*>(&clientAddress), &clientAddressSize);

    if (result.acceptedSocketFD == INVALID_SOCKET) {
        result.errorCode = WSAGetLastError();
        cerr << "accept failed with error: " << result.errorCode << endl;
        return result;
    }

    result.address = clientAddress;
    result.accepted = true;
    return result;
}

void broadcastMessage(const string& message, SOCKET senderSocketFD, int targetRoomNumber) {
    lock_guard<mutex> lock(clientStatesMutex);

    for (const auto& pair : clientStates) {
        SOCKET targetSocket = pair.first;
        const ClientState& state = pair.second;

        if (state.currentRoomNumber == targetRoomNumber && targetSocket != senderSocketFD) {
            int bytesSent = send(targetSocket, message.c_str(), message.length(), 0);
            if (bytesSent == SOCKET_ERROR) {
                cerr << "send to client " << targetSocket << " in room " << targetRoomNumber << " failed with error: " << WSAGetLastError() << endl;
            }
        }
    }
}

string getUsersInRoom(int roomNumber, const string& excludeNickname) {
    string userList = "";
    lock_guard<mutex> lock(clientStatesMutex);
    vector<string> nicknamesInRoom;

    for (const auto& pair : clientStates) {
        const ClientState& state = pair.second;
        if (state.currentRoomNumber == roomNumber && state.nickname != excludeNickname) {
            nicknamesInRoom.push_back(state.nickname);
        }
    }

    for (size_t i = 0; i < nicknamesInRoom.size(); ++i) {
        userList += nicknamesInRoom[i];
        if (i < nicknamesInRoom.size() - 1) {
            userList += ",";
        }
    }
    return userList;
}

bool handleClientCommand(SOCKET clientSocketFD, const string& commandMessage, const string& clientNickname) {
    string trimmedCommand = trim(commandMessage);

    if (trimmedCommand.rfind("COMMAND:JOIN:", 0) == 0) {
        string roomNumberStr = trim(trimmedCommand.substr(13));
        int newRoomNumber;

        try {
            newRoomNumber = stoi(roomNumberStr);
            if (newRoomNumber <= 0) {
                string response = "ERROR: Room number must be a positive integer.\n";
                send(clientSocketFD, response.c_str(), response.length(), 0);
                return true;
            }
        } catch (const invalid_argument& e) {
            string response = "ERROR: Invalid room number format. Please enter a number.\n";
            send(clientSocketFD, response.c_str(), response.length(), 0);
            return true;
        } catch (const out_of_range& e) {
            string response = "ERROR: Room number out of valid range.\n";
            send(clientSocketFD, response.c_str(), response.length(), 0);
            return true;
        }

        unique_lock<mutex> lock(clientStatesMutex);
        auto it = clientStates.find(clientSocketFD);
        int oldRoomNumber = 0;
        if (it != clientStates.end()) {
            oldRoomNumber = it->second.currentRoomNumber;
            if (oldRoomNumber != newRoomNumber) {
                it->second.currentRoomNumber = newRoomNumber;

                lock.unlock();

                if (oldRoomNumber != 0) {
                    string leaveMsg = clientNickname + " has left room number '" + to_string(oldRoomNumber) + "'.\n";
                    broadcastMessage(leaveMsg, clientSocketFD, oldRoomNumber);
                }

                string joinMsg = clientNickname + " has joined room number '" + to_string(newRoomNumber) + "'.\n";
                broadcastMessage(joinMsg, clientSocketFD, newRoomNumber);

            } else {
                string response = "INFO: You are already in room number '" + to_string(newRoomNumber) + "'.\n";
                send(clientSocketFD, response.c_str(), response.length(), 0);
            }
        }
        string response = "ROOM_JOINED:" + to_string(newRoomNumber) + "\n";
        send(clientSocketFD, response.c_str(), response.length(), 0);

        string userList = getUsersInRoom(newRoomNumber, clientNickname);
        string userListMsg = "USER_LIST:" + to_string(newRoomNumber) + ":" + userList + "\n";
        send(clientSocketFD, userListMsg.c_str(), userListMsg.length(), 0);

        cout << "Client " << clientNickname << " moved from room '" << oldRoomNumber << "' to '" << newRoomNumber << "'." << endl;
        return true;
    }
    else if (trimmedCommand == "COMMAND:LEAVE") {
        unique_lock<mutex> lock(clientStatesMutex);
        auto it = clientStates.find(clientSocketFD);
        if (it != clientStates.end()) {
            int oldRoomNumber = it->second.currentRoomNumber;
            if (oldRoomNumber != 0) {
                it->second.currentRoomNumber = 0;

                lock.unlock();

                string leaveMsg = clientNickname + " has left room number '" + to_string(oldRoomNumber) + "'.\n";
                broadcastMessage(leaveMsg, clientSocketFD, oldRoomNumber);

                string response = "ROOM_LEFT:" + to_string(oldRoomNumber) + "\n";
                send(clientSocketFD, response.c_str(), response.length(), 0);

                string userListLobby = getUsersInRoom(0, clientNickname);
                string userListLobbyMsg = "USER_LIST:0:" + userListLobby + "\n";
                send(clientSocketFD, userListLobbyMsg.c_str(), userListLobbyMsg.length(), 0);

                cout << "Client " << clientNickname << " left room '" << oldRoomNumber << "' and moved to lobby (room 0)." << endl;
            } else {
                string response = "INFO: You are already in the lobby.\n";
                send(clientSocketFD, response.c_str(), response.length(), 0);
            }
        }
        return true;
    }

    return false;
}


void HandlingSocket(AcceptedSocket acceptedSocket) {
    string clientNickname = "";
    int currentClientRoomNumber = 0;

    string nicknameRequest = "NICK_REQUIRED\n";
    send(acceptedSocket.acceptedSocketFD, nicknameRequest.c_str(), nicknameRequest.length(), 0);

    bool nicknameSet = false;
    while (!nicknameSet) {
        char buffer[1024];
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(acceptedSocket.acceptedSocketFD, buffer, sizeof(buffer) - 1, 0);

        if (bytesReceived <= 0) {
            cout << "Client " << acceptedSocket.acceptedSocketFD << " disconnected during nickname negotiation." << endl;
            goto cleanup_socket;
        }

        string receivedMessage = trim(buffer);

        if (receivedMessage.rfind("NICK ", 0) == 0) {
            string proposedNickname = trim(receivedMessage.substr(5));

            if (proposedNickname.empty() || proposedNickname.length() > 20) {
                string response = "NICK_REJECTED: Nickname invalid (empty or too long).\n";
                send(acceptedSocket.acceptedSocketFD, response.c_str(), response.length(), 0);
                continue;
            }

            string tempNickname = proposedNickname;
            int tempRoomNumber = 0;

            bool nicknameTaken = false;
            {
                lock_guard<mutex> lock(clientStatesMutex);
                for (const auto& pair : clientStates) {
                    if (pair.second.nickname == proposedNickname) {
                        nicknameTaken = true;
                        break;
                    }
                }

                if (!nicknameTaken) {
                    clientNickname = proposedNickname;
                    currentClientRoomNumber = tempRoomNumber;
                    clientStates[acceptedSocket.acceptedSocketFD] = {clientNickname, currentClientRoomNumber};
                    nicknameSet = true;
                }
            }

            if (nicknameTaken) {
                string response = "NICK_REJECTED: Nickname '" + tempNickname + "' is already taken.\n";
                send(acceptedSocket.acceptedSocketFD, response.c_str(), response.length(), 0);
            } else {
                string response = "NICK_ACCEPTED\n";
                send(acceptedSocket.acceptedSocketFD, response.c_str(), response.length(), 0);
                cout << "Client " << acceptedSocket.acceptedSocketFD << " set nickname to '" << clientNickname << "' and is in lobby (room " << currentClientRoomNumber << ")." << endl;

                string userList = getUsersInRoom(currentClientRoomNumber, clientNickname);
                string userListMsg = "USER_LIST:" + to_string(currentClientRoomNumber) + ":" + userList + "\n";
                send(acceptedSocket.acceptedSocketFD, userListMsg.c_str(), userListMsg.length(), 0);
            }
        } else {
            string response = "ERROR: Please send your nickname using 'NICK <your_name>'.\n";
            send(acceptedSocket.acceptedSocketFD, response.c_str(), response.length(), 0);
        }
    }

    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(acceptedSocket.acceptedSocketFD, buffer, sizeof(buffer) - 1, 0);

        if (bytesReceived == SOCKET_ERROR) {
            int error_code = WSAGetLastError();
            if (error_code == WSAECONNRESET || error_code == WSAENOTSOCK) {
                cout << "Client " << acceptedSocket.acceptedSocketFD << " ('" << clientNickname << "') disconnected unexpectedly (Error: " << error_code << ")." << endl;
            } else {
                cerr << "recv failed for client " << acceptedSocket.acceptedSocketFD << " ('" << clientNickname << "') with error: " << error_code << endl;
            }
            break;
        } else if (bytesReceived == 0) {
            cout << "Client " << acceptedSocket.acceptedSocketFD << " ('" << clientNickname << "') disconnected gracefully." << endl;
            break;
        }

        string receivedMessage = trim(buffer);

        {
            lock_guard<mutex> lock(clientStatesMutex);
            auto it = clientStates.find(acceptedSocket.acceptedSocketFD);
            if (it != clientStates.end()) {
                currentClientRoomNumber = it->second.currentRoomNumber;
            } else {
                cerr << "Error: Client " << clientNickname << " not found in clientStates map during chat loop." << endl;
                break;
            }
        }

        if (receivedMessage.rfind("COMMAND:", 0) == 0) {
            handleClientCommand(acceptedSocket.acceptedSocketFD, receivedMessage, clientNickname);
        } else {
            cout << "Received from client " << acceptedSocket.acceptedSocketFD << " ('" << clientNickname << "') in room '" << currentClientRoomNumber << "': " << receivedMessage << endl;

            string messageToBroadcast = clientNickname + ": " + receivedMessage + "\n";
            broadcastMessage(messageToBroadcast, acceptedSocket.acceptedSocketFD, currentClientRoomNumber);
        }
    }

cleanup_socket:
    string disconnectedNickname = clientNickname;
    int disconnectedRoomNumber = currentClientRoomNumber;
    bool wasInRoom = false;

    {
        lock_guard<mutex> lock(clientStatesMutex);
        auto it = clientStates.find(acceptedSocket.acceptedSocketFD);
        if (it != clientStates.end()) {
            disconnectedNickname = it->second.nickname;
            disconnectedRoomNumber = it->second.currentRoomNumber;
            if (disconnectedRoomNumber != 0) {
                wasInRoom = true;
            }
            clientStates.erase(it);
            cout << "Client " << acceptedSocket.acceptedSocketFD << " ('" << disconnectedNickname << "') removed from lists. Total clients: " << clientStates.size() << endl;
        } else {
            cout << "Client " << acceptedSocket.acceptedSocketFD << " (nickname not set/removed earlier) removed from lists. Total clients: " << clientStates.size() << endl;
        }
    }

    if (!disconnectedNickname.empty() && wasInRoom) {
        broadcastMessage(disconnectedNickname + " has left room number '" + to_string(disconnectedRoomNumber) + "'.\n", acceptedSocket.acceptedSocketFD, disconnectedRoomNumber);
    }

    shutdown(acceptedSocket.acceptedSocketFD, SD_SEND);
    closesocket(acceptedSocket.acceptedSocketFD);
}

void AcceptingNewConnection(SOCKET serverSocketFD) {
    cout << "Server thread for accepting new connections started." << endl;
    while (true) {
        AcceptedSocket acceptedSocket = AcceptIncomeingConnection(serverSocketFD);

        if (acceptedSocket.accepted) {
            cout << "New client accepted. Socket FD: " << acceptedSocket.acceptedSocketFD << endl;
            thread clientHandlerThread(HandlingSocket, acceptedSocket);
            clientHandlerThread.detach();
        } else {
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}

int main() {
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        cerr << "WSAStartup failed: " << iResult << endl;
        return 1;
    }

    SOCKET serverSocketFD = CreateTCPIPv4Socket();
    if (serverSocketFD == INVALID_SOCKET) {
        cerr << "Failed to create server socket. Error: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in address = CreateIPv4Address("127.0.0.1", 8580);

    int binding = bind(serverSocketFD, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (binding == SOCKET_ERROR) {
        cerr << "bind failed with error: " << WSAGetLastError() << endl;
        closesocket(serverSocketFD);
        WSACleanup();
        return 1;
    }

    int listening = listen(serverSocketFD, SOMAXCONN);
    if (listening == SOCKET_ERROR) {
        cerr << "listen failed with error: " << WSAGetLastError() << endl;
        closesocket(serverSocketFD);
        WSACleanup();
        return 1;
    }
    cout << "Server is listening on port 8580..." << endl;

    thread acceptConnectionsThread(AcceptingNewConnection, serverSocketFD);
    acceptConnectionsThread.detach();

    cout << "Main server thread is running. Press Ctrl+C to stop server." << endl;

    while (true) {
        this_thread::sleep_for(chrono::seconds(1));
    }

    closesocket(serverSocketFD);
    WSACleanup();
    return 0;
}