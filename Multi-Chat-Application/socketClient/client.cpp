
#include "socketutil.h"


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

    
    sockaddr_in address = CreateIPv4Address("127.0.0.1",8580);
    int connection_status = connect(clientSocketFD, reinterpret_cast<const sockaddr*>(&address), sizeof(address));

    cout<<"Type a message to send to the server: "<<endl;

    while(true) {
        string message;
        getline(cin, message);

        if (message == "exit" || message == "quit") {
            break;
        }

        int send_status = send(clientSocketFD, message.c_str(), message.size(), 0);
        if (send_status == SOCKET_ERROR) {
            cerr << "send failed with error: " << WSAGetLastError() << endl;
            closesocket(clientSocketFD);
            WSACleanup();
            return 1;
        }
    }

    // char buffer[1024];
    // recv(clientSocketFD,buffer,1024,0);

    // cout<<buffer<<endl;

    closesocket(clientSocketFD);
    WSACleanup();

    return 0;
}