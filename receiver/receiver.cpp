// pc_udp_receiver_with_vjoy.cpp
// UDP receiver with vJoy integration: listens on port 4567, parses controls,
// and feeds them into a vJoy virtual joystick.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include "vJoyInterface.h"
#include <nlohmann/json.hpp>
#include <iphlpapi.h>
#include <fstream>
#include <vector>
#include <utility>


#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "vJoyInterface.lib")

using json = nlohmann::json;

template <typename T>
T clamp(T value, T minVal, T maxVal) {
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return value;
}



// No longer strictly needed but good practice

// This is the single header file from the nlohmann/json library.
// Download it from here: https://github.com/nlohmann/json/blob/develop/single_include/nlohmann/json.hpp

// For convenience, use the nlohmann::json namespace.
using json = nlohmann::json;

// --- A struct to hold the driver settings ---
// This provides clear, descriptive names for the returned values.
struct Settings {
    double steering_range;
    bool is_log;
};


// --- THIS FUNCTION NOW HANDLES EVERYTHING ---
// It checks if the settings file exists, creates it if not, then reads it
// and returns the settings inside the new Settings struct.


void ShowLocalIP() {
    ULONG bufferSize = 15000;
    PIP_ADAPTER_ADDRESSES adapterAddresses = (IP_ADAPTER_ADDRESSES*)malloc(bufferSize);

    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapterAddresses, &bufferSize) == NO_ERROR) {
        bool wifiConnected = false;

        for (PIP_ADAPTER_ADDRESSES adapter = adapterAddresses; adapter != nullptr; adapter = adapter->Next) {
            // Only Wi-Fi adapters that are up
            if (adapter->IfType == IF_TYPE_IEEE80211 && adapter->OperStatus == IfOperStatusUp) {
                for (PIP_ADAPTER_UNICAST_ADDRESS ua = adapter->FirstUnicastAddress; ua != nullptr; ua = ua->Next) {
                    SOCKADDR_IN* sa_in = (SOCKADDR_IN*)ua->Address.lpSockaddr;
                    char ipStr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(sa_in->sin_addr), ipStr, INET_ADDRSTRLEN);

                    std::cout << "Use this in IP in app: " << ipStr << std::endl;
                    wifiConnected = true;
                }
            }
        }

        if (!wifiConnected) {
            std::cout << "Wi-Fi is not connected" << std::endl;
        }
    }
    else {
        std::cout << "Failed to get adapter addresses" << std::endl;
    }

    free(adapterAddresses);
}

// Map a normalized [-1,1] value to vJoy axis range [0,0x8000]
LONG MapToVJoyAxis(double norm) {
    if (norm < -1.0) norm = -1.0;
    if (norm > 1.0) norm = 1.0;
    return static_cast<LONG>(16384 + norm * 16384);
}

double userSteering() {
    double userRange = 900.0; // Default
    std::string input;

    std::cout << "Enter steering range (min 90, max 2520), or press Enter for default (900) :";
    std::getline(std::cin, input);

    if (!input.empty()) {
        try {
            double tempRange = std::stod(input);
            if (tempRange >= 90.0 && tempRange <= 2520.0) {
                userRange = tempRange;
            }
            else {
                std::cerr << "Range too low or high. Using default (900).\n";
            }
        }
        catch (...) {
            std::cerr << "Invalid input. Using default (900).\n";
        }
    }

    std::cout << "Using steering range: " << userRange << "\n";

    return userRange;
}

UINT vJoyId = 1;

void SetVJoyButton(UINT btnNumber, bool pressed)
{
    // btnNumber starts at 1
    SetBtn(pressed, vJoyId, static_cast<UCHAR>(btnNumber));
}


void pressEnterToExit() {
    std::cout << "Press Enter to exit...";
    std::cin.ignore(10000, '\n');
    std::cin.get();
}

void checkVJoyOwnership(UINT id) {
    VjdStat status = GetVJDStatus(id);

    switch (status) {
    case VJD_STAT_FREE:
        std::cout << "Device " << id << " is FREE.\n";
        break;

    case VJD_STAT_OWN:
        std::cout << "Device " << id << " is OWNED by this process.\n";
        break;

    case VJD_STAT_BUSY:
        std::cout << "Device " << id << " is BUSY (owned by another process).\n";
        break;

    case VJD_STAT_MISS:
        std::cout << "Device " << id << " is not configured in vJoyConf.\n";
        break;

    default:
        std::cout << "Unknown device status.\n";
        break;
    }
}

int main() {

    ShowLocalIP(); // Print local IP address for debugging


    // 1. Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        pressEnterToExit();
        return 1;
    }

    // 2. Initialize vJoy
    //UINT vJoyId = 1;
    if (!vJoyEnabled()) {
        std::cout << "\n!!  vJoy Device " << vJoyId << " is not available.\n";
        std::cout << " To fix this:\n";
        //std::cout << "watch this on yt:\n";
        //std::cout << "OR:\n";
        std::cout << "1. Press the Windows key and search for \"Configure vJoy or vJoyConf\".\n";
        std::cout << "2. Open the vJoy Configuration Tool.\n";
        std::cout << "3. Check 'Enable vJoy' in bottom corner.\n";
        std::cout << "4. Select Device " << vJoyId << ".\n";
        std::cout << "5. Enable all axes (like X, Y) and set number of buttons or just set 32.\n";
        std::cout << "6. Turn off force feedback for better experience.\n";
        std::cout << "7. Click 'Apply', then close the tool and restart this app.\n\n";
        WSACleanup();
        pressEnterToExit();
        return 1;
    }
    VjdStat status = GetVJDStatus(vJoyId);
    if (status == VJD_STAT_OWN || status == VJD_STAT_FREE) {
        if (!AcquireVJD(vJoyId)) {
            std::cerr << "Failed to acquire vJoy device #" << vJoyId << "\n";
            checkVJoyOwnership(vJoyId);
            WSACleanup();
            pressEnterToExit();
            return 1;
        }
    }
    else {
        std::cerr << "vJoy device #" << vJoyId << " not available (status=" << status << ")\n";
        checkVJoyOwnership(vJoyId);
        WSACleanup();
        pressEnterToExit();
        return 1;
    }
    std::cout << "vJoy device #" << vJoyId << " acquired\n";

    // 3. Create UDP socket
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "socket() failed: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    // 4. Bind socket to port 4567
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(4567);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed: " << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    std::cout << "Listening for UDP on port 4567...\n";


    //user steering range

    double userRange= userSteering();



    // 5. Receive loop
    const int bufSize = 512;
    char buffer[bufSize];
    sockaddr_in client;
    int clientLen = sizeof(client);



    while (true) {
        int bytes = recvfrom(sock, buffer, bufSize - 1, 0,
            reinterpret_cast<sockaddr*>(&client), &clientLen);
        if (bytes == SOCKET_ERROR) {
            std::cerr << "recvfrom() error: " << WSAGetLastError() << "\n";
            break;
        }
        buffer[bytes] = '\0';
        std::string msg(buffer);

        wchar_t ipStr[INET_ADDRSTRLEN]; // Wide-character buffer for IP address
        InetNtopW(AF_INET, &client.sin_addr, ipStr, INET_ADDRSTRLEN);

        
            std::cout << "[log]Received" << " -> " << msg << std::endl;
        
        // Parse values

        try {
            auto j = json::parse(buffer);

            double steering = j.at("steering").get<double>();  // e.g. 450.0
            double throttle = j.at("throttle").get<double>();  // 0.75
            double brake = j.at("brake").get<double>();     // 0.25
            double zaxis = j.at("zaxis").get<double>();
            //double dx = j.at("dx").get<double>(); // Mouse relative movement

            j.erase("steering");
            j.erase("throttle");
            j.erase("brake");
            j.erase("zaxis");
            //j.erase("dx");

            // 6. Feed to vJoy axes
            double normSteer = steering / userRange;

            LONG vJoyValue = clamp(MapToVJoyAxis(normSteer), static_cast <LONG> (0), static_cast <LONG> (32768));


            //SetAxis(MapToVJoyAxis(normSteer), vJoyId, HID_USAGE_X);

            SetAxis(vJoyValue, vJoyId, HID_USAGE_X);
            SetAxis(MapToVJoyAxis(throttle * 2 - 1), vJoyId, HID_USAGE_Y);
            SetAxis(MapToVJoyAxis(brake * 2 - 1), vJoyId, HID_USAGE_Z);
            SetAxis(MapToVJoyAxis(zaxis * 2 - 1), vJoyId, HID_USAGE_RZ);
            SetVJoyButton(1, j.value("horn", false));

            //mouse relative movement
            //moveMouseRelative(dx);

            j.erase("horn");

            for (json::iterator it = j.begin(); it != j.end(); ++it) {
                std::string key = it.key();
                int buttonId = std::stoi(key);
                bool value = it.value();
                SetVJoyButton(buttonId, j.value(key, false));
                j.erase(key); // Remove processed button
            }


        }
        catch (json::exception& e) {
            std::cerr << "JSON parse error: " << e.what() << "\n";
        }

    }

    // 7. Cleanup
    RelinquishVJD(vJoyId);
    closesocket(sock);
    WSACleanup();
    return 0;
}
