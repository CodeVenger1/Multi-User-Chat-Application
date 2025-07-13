#include "socketutil.h"


SOCKET CreateTCPIPv4Socket() {
    return socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

sockaddr_in CreateIPv4Address(const string& ip, int port) {
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if(ip.empty()) {
        address.sin_addr.s_addr = INADDR_ANY; // Use any available address if no IP is provided
    } else {
        InetPtonA(AF_INET, ip.c_str(), &address.sin_addr);
    }
    return address;
}
