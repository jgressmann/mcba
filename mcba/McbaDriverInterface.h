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

#pragma once

#include <initguid.h>

//
// Define an Interface Guid so that app can find the device and talk to it.
//

DEFINE_GUID (GUID_DEVINTERFACE_MCBA,
    0xc07c7033,0x72cd,0x47be,0x96,0x70,0x96,0x14,0x45,0x8d,0x5f,0x6e);
// {c07c7033-72cd-47be-9670-9614458d5f6e}

/* controller area network (CAN) kernel definitions */

/* special address description flags for the CAN_ID */
#define MCBA_CAN_EFF_FLAG 0x80000000U /* EFF/SFF is set in the MSB */
#define MCBA_CAN_RTR_FLAG 0x40000000U /* remote transmission request */
#define MCBA_CAN_ERR_FLAG 0x20000000U /* error message frame */

/* valid bits in CAN ID for frame formats */
#define MCBA_CAN_SFF_MASK 0x000007FFU /* standard frame format (SFF) */
#define MCBA_CAN_EFF_MASK 0x1FFFFFFFU /* extended frame format (EFF) */
#define MCBA_CAN_ERR_MASK 0x1FFFFFFFU /* omit EFF, RTR, ERR flags */

/*
 * Controller Area Network Identifier structure
 *
 * bit 0-28	: CAN identifier (11/29 bit)
 * bit 29	: error message frame flag (0 = data frame, 1 = error message)
 * bit 30	: remote transmission request flag (1 = rtr frame)
 * bit 31	: frame format flag (0 = standard 11 bit, 1 = extended 29 bit)
 */
typedef UINT32 MCBA_CAN_ID;

#define MCBA_CAN_SFF_ID_BITS		11
#define MCBA_CAN_EFF_ID_BITS		29


/* CAN payload length and DLC definitions according to ISO 11898-1 */
#define MCBA_CAN_MAX_DLC 8
#define MCBA_CAN_MAX_DLEN 8

#define MCBA_BATCH_WRITE_MAX_SIZE 16


typedef struct _MCBA_CAN_MSG {
    MCBA_CAN_ID Id;
    UINT8 Dlc;
    UINT8 Padding[3];
    UINT8 Data[MCBA_CAN_MAX_DLEN];
} MCBA_CAN_MSG, *PMCBA_CAN_MSG;

typedef struct _MCBA_CAN_MSG_DATA {
    MCBA_CAN_MSG Msg;
    ULONGLONG SystemTimeReceived;
} MCBA_CAN_MSG_DATA, *PMCBA_CAN_MSG_DATA;

typedef enum _MCBA_BITRATE {
    MCBA_BITRATE_UNKOWN = 0,
    MCBA_BITRATE_20000 = 20000,
    MCBA_BITRATE_33333 = 33333,
    MCBA_BITRATE_50000 = 50000,
    MCBA_BITRATE_80000 = 80000,
    MCBA_BITRATE_83333 = 83333,
    MCBA_BITRATE_100000 = 100000,
    MCBA_BITRATE_125000 = 125000,
    MCBA_BITRATE_150000 = 150000,
    MCBA_BITRATE_175000 = 175000,
    MCBA_BITRATE_200000 = 200000,
    MCBA_BITRATE_225000 = 225000,
    MCBA_BITRATE_250000 = 250000,
    MCBA_BITRATE_275000 = 275000,
    MCBA_BITRATE_300000 = 300000,
    MCBA_BITRATE_500000 = 500000,
    MCBA_BITRATE_625000 = 625000,
    MCBA_BITRATE_800000 = 800000,
    MCBA_BITRATE_1000000 = 1000000
} MCBA_BITRATE, * PMCBA_BITRATE;

typedef struct _MCBA_DEVICE_STATS {
    ULONGLONG TxErrorCount;
    ULONGLONG RxErrorCount;
    ULONGLONG RxBufferOverflow;
    ULONGLONG TxBusOff;
    ULONGLONG RxLost;
} MCBA_DEVICE_STATS, * PMCBA_DEVICE_STATS;

typedef struct _MCBA_DEVICE_STATUS {
    MCBA_DEVICE_STATS Stats;
    MCBA_BITRATE Bitrate;
    UINT8 UsbSoftwareVersionMajor;
    UINT8 UsbSoftwareVersionMinor;
    UINT8 CanSoftwareVersionMajor;
    UINT8 CanSoftwareVersionMinor;
    BOOLEAN TerminationEnabled;
} MCBA_DEVICE_STATUS, *PMCBA_DEVICE_STATUS;


typedef struct _MCBA_FILE_STATS {
    ULONGLONG RxLost;
} MCBA_FILE_STATS, * PMCBA_FILE_STATS;

/* IOCTLs */
#define MCBA_FILE_DEVICE 0x8112 
#define MCBA_IOCTL_OFFSET 0x800

#define MCBA_IOCTL_DEVICE_BITRATE_SET CTL_CODE(MCBA_FILE_DEVICE, MCBA_IOCTL_OFFSET+0, METHOD_IN_DIRECT, FILE_WRITE_DATA)
#define MCBA_IOCTL_DEVICE_BITRATE_GET CTL_CODE(MCBA_FILE_DEVICE, MCBA_IOCTL_OFFSET+1, METHOD_OUT_DIRECT, FILE_READ_DATA)
#define MCBA_IOCTL_DEVICE_RESET CTL_CODE(MCBA_FILE_DEVICE, MCBA_IOCTL_OFFSET+2, METHOD_NEITHER, FILE_WRITE_DATA)
#define MCBA_IOCTL_DEVICE_STATS_CLEAR CTL_CODE(MCBA_FILE_DEVICE, MCBA_IOCTL_OFFSET+3, METHOD_NEITHER, FILE_WRITE_DATA)
#define MCBA_IOCTL_DEVICE_STATS_GET CTL_CODE(MCBA_FILE_DEVICE, MCBA_IOCTL_OFFSET+4, METHOD_OUT_DIRECT, FILE_READ_DATA)
#define MCBA_IOCTL_DEVICE_TERMINATION_DISABLE CTL_CODE(MCBA_FILE_DEVICE, MCBA_IOCTL_OFFSET+5, METHOD_NEITHER, FILE_WRITE_DATA)
#define MCBA_IOCTL_DEVICE_TERMINATION_ENABLE CTL_CODE(MCBA_FILE_DEVICE, MCBA_IOCTL_OFFSET+6, METHOD_NEITHER, FILE_WRITE_DATA)
#define MCBA_IOCTL_DEVICE_STATUS_GET CTL_CODE(MCBA_FILE_DEVICE, MCBA_IOCTL_OFFSET+7, METHOD_OUT_DIRECT, FILE_READ_DATA)
#define MCBA_IOCTL_HOST_CAN_FRAME_READ_AT_LEAST_ONE CTL_CODE(MCBA_FILE_DEVICE, MCBA_IOCTL_OFFSET+101, METHOD_OUT_DIRECT, FILE_READ_DATA)
#define MCBA_IOCTL_HOST_CAN_FRAME_READ_NON_BLOCKING CTL_CODE(MCBA_FILE_DEVICE, MCBA_IOCTL_OFFSET+102, METHOD_OUT_DIRECT, FILE_READ_DATA)
#define MCBA_IOCTL_HOST_CAN_FRAME_WRITE_AT_LEAST_ONE CTL_CODE(MCBA_FILE_DEVICE, MCBA_IOCTL_OFFSET+103, METHOD_IN_DIRECT, FILE_WRITE_DATA)
#define MCBA_IOCTL_HOST_CAN_FRAME_WRITE_NON_BLOCKING CTL_CODE(MCBA_FILE_DEVICE, MCBA_IOCTL_OFFSET+104, METHOD_IN_DIRECT, FILE_WRITE_DATA)
#define MCBA_IOCTL_HOST_FILE_STATS_CLEAR CTL_CODE(MCBA_FILE_DEVICE, MCBA_IOCTL_OFFSET+105, METHOD_NEITHER, FILE_WRITE_DATA)
#define MCBA_IOCTL_HOST_FILE_STATS_GET CTL_CODE(MCBA_FILE_DEVICE, MCBA_IOCTL_OFFSET+106, METHOD_OUT_DIRECT, FILE_READ_DATA)



