#ifndef SOCKETUTIL_SOCKETUTIL_H
#define SOCKETUTIL_SOCKETUTIL_H


#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include <map>
#include <condition_variable>
#include <atomic>
using namespace std;
#pragma comment(lib, "Ws2_32.lib")

SOCKET CreateTCPIPv4Socket();

sockaddr_in CreateIPv4Address(const string& ip, int port);

#endif //SOCKETUTIL_SOCKETUTIL_H