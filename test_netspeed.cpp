// test_netspeed.cpp - Standalone test for NetSpeedMonitor
// Build: cl /EHsc /std:c++17 test_netspeed.cpp NetSpeedMonitor.cpp /I../src /Fe:test_netspeed.exe /link iphlpapi.lib ws2_32.lib
#include "../src/NetSpeedMonitor.h"
#include <cstdio>
#include <windows.h>
#include <tlhelp32.h>

int main() {
    printf("=== NetSpeedMonitor Test ===\n");
    printf("Searching for QQ.exe TCP connections...\n\n");

    NetSpeedMonitor mon;
    mon.Start();

    // Find QQ's PID
    DWORD qqPid = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(pe) };
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"QQ.exe") == 0) {
                    qqPid = pe.th32ProcessID;
                    printf("Found QQ.exe PID: %lu\n", qqPid);
                    break;
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    if (qqPid == 0) {
        printf("QQ.exe not running. Starting test with 5-sec intervals...\n");
        printf("Please start QQ.exe and re-run this test.\n");
        return 1;
    }

    // First sample (establishes baseline, no delta)
    printf("\n--- Sample 1 (baseline) ---\n");
    ULONGLONG s1, r1;
    mon.QueryDelta(qqPid, s1, r1);
    printf("delta: sent=%I64u recv=%I64u (should be 0)\n", s1, r1);

    // Wait 5 seconds
    printf("Waiting 5 seconds...\n");
    Sleep(5000);

    // Second sample
    printf("\n--- Sample 2 (+5s) ---\n");
    ULONGLONG s2, r2;
    mon.QueryDelta(qqPid, s2, r2);
    printf("delta: sent=%I64u bytes  recv=%I64u bytes\n", s2, r2);
    printf("speed: sent=%.2f Kbps  recv=%.2f Kbps\n",
           (double)s2 * 8.0 / 1000.0 / 5.0, (double)r2 * 8.0 / 1000.0 / 5.0);

    // Wait another 5 seconds
    printf("\nWaiting 5 seconds...\n");
    Sleep(5000);

    // Third sample
    printf("\n--- Sample 3 (+10s) ---\n");
    ULONGLONG s3, r3;
    mon.QueryDelta(qqPid, s3, r3);
    printf("delta: sent=%I64u bytes  recv=%I64u bytes\n", s3, r3);
    printf("speed: sent=%.2f Kbps  recv=%.2f Kbps\n",
           (double)s3 * 8.0 / 1000.0 / 5.0, (double)r3 * 8.0 / 1000.0 / 5.0);

    mon.Stop();
    printf("\nDone.\n");
    return 0;
}
