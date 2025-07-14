#include "socketutil.h"

map<SOCKET, string> clientNicknames;
mutex nicknamesMutex;

vector<SOCKET> connectedClientSockets;
mutex clientSocketsMutex;

struct AcceptedSocket {
    SOCKET acceptedSocketFD;
    sockaddr_in address;
    int errorCode;
    bool accepted;
};

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

void broadcastMessage(const string& message, SOCKET senderSocketFD) {
    lock_guard<mutex> lock(clientSocketsMutex);

    for (SOCKET client_sock : connectedClientSockets) {
        if (client_sock != senderSocketFD) {
            int bytesSent = send(client_sock, message.c_str(), message.length(), 0);
            if (bytesSent == SOCKET_ERROR) {
                cerr << "send to client " << client_sock << " failed with error: " << WSAGetLastError() << endl;
            }
        }
    }
}

void HandlingSocket(AcceptedSocket acceptedSocket) {
    string clientNickname = "UnnamedClient_" + to_string(acceptedSocket.acceptedSocketFD);

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

        string receivedMessage(buffer);
        size_t last_char_pos = receivedMessage.find_last_not_of(" \n\r\t");
        if (last_char_pos == string::npos) {
            receivedMessage.clear();
        } else {
            receivedMessage.resize(last_char_pos + 1);
        }

        if (receivedMessage.rfind("NICK ", 0) == 0) {
            string proposedNickname = receivedMessage.substr(5);

            if (proposedNickname.empty() || proposedNickname.length() > 20) {
                string response = "NICK_REJECTED: Nickname invalid (empty or too long).\n";
                send(acceptedSocket.acceptedSocketFD, response.c_str(), response.length(), 0);
                continue;
            }

            {
                lock_guard<mutex> lock(nicknamesMutex);

                bool nicknameTaken = false;
                for (const auto& pair : clientNicknames) {
                    if (pair.second == proposedNickname) {
                        nicknameTaken = true;
                        break;
                    }
                }

                if (nicknameTaken) {
                    string response = "NICK_REJECTED: Nickname '" + proposedNickname + "' is already taken.\n";
                    send(acceptedSocket.acceptedSocketFD, response.c_str(), response.length(), 0);
                } else {
                    clientNickname = proposedNickname;
                    clientNicknames[acceptedSocket.acceptedSocketFD] = clientNickname;
                    nicknameSet = true;
                    string response = "NICK_ACCEPTED\n";
                    send(acceptedSocket.acceptedSocketFD, response.c_str(), response.length(), 0);
                    cout << "Client " << acceptedSocket.acceptedSocketFD << " set nickname to '" << clientNickname << "'." << endl;

                    {
                        lock_guard<mutex> clientLock(clientSocketsMutex);
                        connectedClientSockets.push_back(acceptedSocket.acceptedSocketFD);
                    }
                    cout << "Client " << acceptedSocket.acceptedSocketFD << " added to connected list. Total: " << connectedClientSockets.size() << endl;

                    broadcastMessage(clientNickname + " has joined the chat.\n", acceptedSocket.acceptedSocketFD);
                }
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
                cout << clientNickname << " disconnected unexpectedly (Error: " << error_code << ")." << endl;
            } else {
                cerr << "recv failed for client " << clientNickname << " with error: " << error_code << endl;
            }
            break;
        } else if (bytesReceived == 0) {
            cout << "Client " <<  clientNickname << " disconnected gracefully." << endl;
            break;
        }

        buffer[bytesReceived] = '\0';
        string receivedMessage(buffer);
        size_t last_char_pos = receivedMessage.find_last_not_of(" \n\r\t");
        if (last_char_pos != string::npos) {
            receivedMessage.resize(last_char_pos + 1);
        } else {
            receivedMessage.clear();
        }

        cout <<  clientNickname << " : " << receivedMessage << endl;

        string messageToBroadcast = clientNickname + ": " + receivedMessage + "\n";
        broadcastMessage(messageToBroadcast, acceptedSocket.acceptedSocketFD);
    }

cleanup_socket:
    {
        lock_guard<mutex> clientLock(clientSocketsMutex);
        connectedClientSockets.erase(remove_if(connectedClientSockets.begin(), connectedClientSockets.end(),
                                                    [fd = acceptedSocket.acceptedSocketFD](SOCKET s) { return s == fd; }),
                                     connectedClientSockets.end());
    }

    {
        lock_guard<mutex> nicknameLock(nicknamesMutex);
        if (clientNicknames.count(acceptedSocket.acceptedSocketFD)) {
            string disconnectedNickname = clientNicknames[acceptedSocket.acceptedSocketFD];
            clientNicknames.erase(acceptedSocket.acceptedSocketFD);
            cout << "Client " <<  disconnectedNickname << " removed from lists. Total: " << connectedClientSockets.size() << endl;
            broadcastMessage(disconnectedNickname + " has left the chat.\n", acceptedSocket.acceptedSocketFD);
        } else {
            cout << "Client " << acceptedSocket.acceptedSocketFD << " (nickname not set) removed from lists. Total: " << connectedClientSockets.size() << endl;
        }
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
