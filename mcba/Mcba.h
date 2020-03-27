#pragma once

#include <ntdef.h>

EXTERN_C_START

/* definitions mostly copied from Linux kernel drivers/net/can/usb/mcba_usb.c */

/* RX buffer must be bigger than msg size since at the
 * beggining USB messages are stacked.
 */
#define MCBA_USB_RX_BUFF_SIZE 64
#define MCBA_USB_TX_BUFF_SIZE (sizeof(struct mcba_usb_msg))

 /* MCBA endpoint numbers */
#define MCBA_USB_EP_IN 1
#define MCBA_USB_EP_OUT 1

/* Microchip command id */
#define MBCA_CMD_RECEIVE_MESSAGE 0xE3
#define MBCA_CMD_I_AM_ALIVE_FROM_CAN 0xF5
#define MBCA_CMD_I_AM_ALIVE_FROM_USB 0xF7
#define MBCA_CMD_CHANGE_BIT_RATE 0xA1
#define MBCA_CMD_TRANSMIT_MESSAGE_EV 0xA3
#define MBCA_CMD_SETUP_TERMINATION_RESISTANCE 0xA8
#define MBCA_CMD_READ_FW_VERSION 0xA9
#define MBCA_CMD_NOTHING_TO_SEND 0xFF
#define MBCA_CMD_TRANSMIT_MESSAGE_RSP 0xE2

#define MCBA_VER_REQ_USB 1
#define MCBA_VER_REQ_CAN 2

#define MCBA_SIDL_EXID_MASK 0x8
#define MCBA_DLC_MASK 0xf
#define MCBA_DLC_RTR_MASK 0x40

#define MCBA_CAN_STATE_WRN_TH 95
#define MCBA_CAN_STATE_ERR_PSV_TH 127

#define MCBA_TERMINATION_DISABLED 0
#define MCBA_TERMINATION_ENABLED 120



#include <pshpack1.h>
/* CAN frame */
struct mcba_usb_msg_can {
	UINT8 cmd_id;
	UINT16 eid; /* big endian */
	UINT16 sid; /* big endian */
	UINT8 dlc;
	UINT8 data[8];
	UINT8 timestamp[4];
	UINT8 checksum;
};

/* command frame */
struct mcba_usb_msg {
	UINT8 cmd_id;
	UINT8 unused[18];
};

struct  mcba_usb_msg_ka_usb {
	UINT8 cmd_id;
	UINT8 termination_state;
	UINT8 soft_ver_major;
	UINT8 soft_ver_minor;
	UINT8 unused[15];
};

struct  mcba_usb_msg_ka_can {
	UINT8 cmd_id;
	UINT8 tx_err_cnt;
	UINT8 rx_err_cnt;
	UINT8 rx_buff_ovfl;
	UINT8 tx_bus_off;
	UINT16 can_bitrate; /* big endian format */
	UINT16 rx_lost; /* little endian format */
	UINT8 can_stat;
	UINT8 soft_ver_major;
	UINT8 soft_ver_minor;
	UINT8 debug_mode;
	UINT8 test_complete;
	UINT8 test_result;
	UINT8 unused[4];
};

struct  mcba_usb_msg_change_bitrate {
	UINT8 cmd_id;
	UINT16 bitrate; /* big endian format */
	UINT8 unused[16];
};

struct  mcba_usb_msg_termination {
	UINT8 cmd_id;
	UINT8 termination;
	UINT8 unused[17];
};

struct  mcba_usb_msg_fw_ver {
	UINT8 cmd_id;
	UINT8 pic;
	UINT8 unused[17];
};

#include <poppack.h>

EXTERN_C_END
