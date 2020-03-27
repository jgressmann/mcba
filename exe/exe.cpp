/* The MIT License (MIT)
 *
 * Copyright (c) 2020 Jean Gressmann <jean@0x42.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// exe.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include "Windows.h"
#include <devioctl.h>
#include <stdio.h>
#include "../mcba/McbaDriverInterface.h"
#include "Misc.h"


int main()
{
    MCBA_CAN_MSG_DATA messages[100];
    MCBA_CAN_MSG echoMesages[3];
    MCBA_DEVICE_STATS stats;

    echoMesages[0].Dlc = 8;
    echoMesages[0].Id = 0x100;
    echoMesages[0].Data[0] = 0x01;
    echoMesages[0].Data[1] = 0x23;
    echoMesages[0].Data[2] = 0x45;
    echoMesages[0].Data[3] = 0x67;
    echoMesages[0].Data[4] = 0x89;
    echoMesages[0].Data[5] = 0xab;
    echoMesages[0].Data[6] = 0xcd;
    echoMesages[0].Data[7] = 0xef;
    echoMesages[1].Dlc = 8;
    echoMesages[1].Id = 0x100;
    echoMesages[1].Data[7] = 0x01;
    echoMesages[1].Data[6] = 0x23;
    echoMesages[1].Data[5] = 0x45;
    echoMesages[1].Data[4] = 0x67;
    echoMesages[1].Data[3] = 0x89;
    echoMesages[1].Data[2] = 0xab;
    echoMesages[1].Data[1] = 0xcd;
    echoMesages[1].Data[0] = 0xef;
    echoMesages[2].Id = 0x100;
    echoMesages[2].Data[0] = 0xfe;
    echoMesages[2].Data[1] = 0xfe;
    echoMesages[2].Data[2] = 0xfe;
    echoMesages[2].Data[3] = 0xfe;
    echoMesages[2].Data[4] = 0xfe;
    echoMesages[2].Data[5] = 0xfe;
    echoMesages[2].Data[6] = 0xfe;
    echoMesages[2].Data[7] = 0xfe;

    HANDLE deviceHandle = OpenDevice(&GUID_DEVINTERFACE_MCBA, TRUE);
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to open device\n");
        return 1;
    }

    MCBA_BITRATE bitrate = MCBA_BITRATE_500000;
    fprintf(stdout, "Set bitrate to %d [bps]\n", (int)bitrate);
    
    if (!DeviceIoControl(deviceHandle, MCBA_IOCTL_DEVICE_BITRATE_SET, &bitrate, sizeof(bitrate), NULL, 0, NULL, NULL)) {
        DWORD error = GetLastError();
    }

    fprintf(stdout, "Get bitrate\n");

    if (!DeviceIoControl(deviceHandle, MCBA_IOCTL_DEVICE_BITRATE_GET, NULL, 0, &bitrate, sizeof(bitrate), NULL, NULL)) {
        DWORD error = GetLastError();
    }

    fprintf(stdout, "Actual bitrate %d [bps]\n", (int)bitrate);


    while (1) {
        DWORD r;
        if (!DeviceIoControl(deviceHandle, MCBA_IOCTL_DEVICE_STATS_GET, NULL, 0, &stats, sizeof(stats), &r, NULL)) {
            DWORD error = GetLastError();
            break;
        }

        fprintf(stdout, "RX err=%llu buf ovrfl=%llu lost=%llu TX err=%llu bus off=%llu\n", 
            stats.RxErrorCount, stats.RxBufferOverflow, stats.RxLost, stats.TxErrorCount, stats.TxBusOff);
        

        if (DeviceIoControl(deviceHandle, MCBA_IOCTL_HOST_CAN_FRAME_READ_AT_LEAST_ONE, NULL, 0, messages, sizeof(messages), &r, NULL)) {
        //if (ReadFile(deviceHandle, messages, sizeof(messages), &r, NULL)) {
            DWORD count = r / sizeof(MCBA_CAN_MSG_DATA);
            fprintf(stdout, "%u messages\n", (unsigned)count);
            for (DWORD i = 0; i < count; ++i) {
                const MCBA_CAN_MSG_DATA& m = messages[i];
                fprintf(stdout, "0x%x [%u] ", m.Msg.Id, m.Msg.Dlc);
                for (UINT8 j = 0; j < m.Msg.Dlc; ++j) {
                    fprintf(stdout, "%02X ", m.Msg.Data[j]);
                }
                fprintf(stdout, "\n");
            }
        } else {
            DWORD error = GetLastError();
            break;
        }

        

        if (!WriteFile(deviceHandle, &echoMesages[0], sizeof(MCBA_CAN_MSG), NULL, NULL)) {
            DWORD error = GetLastError();
            break;
        }

        if (!WriteFile(deviceHandle, &echoMesages[1], 2 * sizeof(MCBA_CAN_MSG), NULL, NULL)) {
            DWORD error = GetLastError();
            break;
        }
    }

    CloseHandle(deviceHandle);

    return 0;
}
