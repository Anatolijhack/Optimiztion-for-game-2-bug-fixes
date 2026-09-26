#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>   // <-- ќЅя«ј“≈Ћ№Ќќ раньше Windows.h
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>    // <-- теперь Windows.h идЄт ѕќ—Ћ≈ winsock2.h
#include <vector>
#include <string>
#include <algorithm>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

class NetworkEnvironmentDetector
{
public:
    // ѕровер€ет, есть ли в системе активный VPN/туннельный адаптер.
    // Ќе пытаетс€ определить  ќЌ –≈“Ќџ… процесс Ч просто честно
    // отключает сетевую логику оптимизации целиком, если такой адаптер есть.
    static bool isVpnOrTunnelActive()
    {
        ULONG bufferSize = 0;
        GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &bufferSize);

        std::vector<BYTE> buffer(bufferSize);
        PIP_ADAPTER_ADDRESSES addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());

        if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, addresses, &bufferSize) != NO_ERROR)
            return false;

        for (PIP_ADAPTER_ADDRESSES adapter = addresses; adapter != nullptr; adapter = adapter->Next)
        {
            if (adapter->OperStatus != IfOperStatusUp)
                continue; // интересуют только активные адаптеры

            // 1. “ип интерфейса Ч PPP или туннель почти всегда VPN
            if (adapter->IfType == IF_TYPE_PPP || adapter->IfType == IF_TYPE_TUNNEL)
                return true;

            // 2. ќписание адаптера Ч TAP-Windows, WinTun и подобные
            // используютс€ подавл€ющим большинством VPN-клиентов
            std::wstring desc(adapter->Description ? adapter->Description : L"");
            std::wstring friendlyName(adapter->FriendlyName ? adapter->FriendlyName : L"");

            std::transform(desc.begin(), desc.end(), desc.begin(), ::towlower);
            std::transform(friendlyName.begin(), friendlyName.end(), friendlyName.begin(), ::towlower);

            static const std::vector<std::wstring> vpnAdapterMarkers = {
                L"tap-windows", L"wintun", L"vpn", L"tunnel",
                L"wireguard", L"openvpn", L"nordlynx", L"tap0901"
            };

            for (auto& marker : vpnAdapterMarkers)
            {
                if (desc.find(marker) != std::wstring::npos ||
                    friendlyName.find(marker) != std::wstring::npos)
                {
                    return true;
                }
            }
        }

        return false;
    }
};