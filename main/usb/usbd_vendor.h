/*
 * USB device-side definitions for the PUD vendor class, ESP32-S3 port of
 * Pico-USB-Display/src/cherryusb/usbd_vendor.h.
 *
 * Differences from the RP2350 original:
 *  - VID/PID are Espressif's (0x303A) with a PUD-specific product id, so the
 *    device no longer shares Raspberry Pi's 0x2E8A:0x0001 with the Pico and
 *    ZXRTT builds; the host driver's id table lists both.
 *  - No picoboot reset interface (that one reboots an RP2350 into BOOTSEL);
 *    a single vendor interface carries EP1/EP2/EP4.
 */

#ifndef _USBD_VENDOR_H_
#define _USBD_VENDOR_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_core.h"

#define VENDOR_ID 0x303A
#define PRODUCT_ID 0x3503

#define USBD_MAX_POWER 500

#define EP0_IN_ADDR (USB_EP_DIR_IN | 0)
#define EP0_OUT_ADDR (USB_EP_DIR_OUT | 0)
#define EP1_OUT_ADDR (USB_EP_DIR_OUT | 1)
#define EP2_IN_ADDR (USB_EP_DIR_IN | 2)
#define EP4_IN_ADDR (USB_EP_DIR_IN | 4)

#define REQ_EP0_OUT 0X00
#define REQ_EP0_IN 0X01
#define REQ_EP1_OUT 0X02 /* retired in protocol v2; the device stalls it */
#define REQ_EP2_IN 0X03
#define REQ_EP3_OUT 0X04 /* reserved, not implemented */
#define REQ_EP4_IN 0X05
/* Runtime parameters ride a control OUT (REQ_SET_PARAM) carrying
 * struct req_set_param -- a handful of bytes, set rarely, on the one channel
 * that needs no descriptor change next to the EP1 image stream. */
#define REQ_SET_PARAM 0X06

/*
 * Largest single EP1 transfer the firmware accepts, in bytes (header
 * included).  The same number sizes ep1_read_buffer and each decoder frame
 * slot, and the device advertises it via PUD_CMD_GET_CAPS so the host bands
 * accordingly.  32 KB tier: three slots plus the read buffer cost 128 KB of
 * static RAM; the display board's data bus occupies the octal-PSRAM pins, so
 * there is no external RAM to fall back on (see main/include/config.h).
 */
#ifndef PUD_MAX_TRANSFER
#define PUD_MAX_TRANSFER (64 * 1024)
#endif

/* The staging buffer only has to hold one accepted transfer. */
#define EP1_RD_BUF_SIZE PUD_MAX_TRANSFER

/* The first EP1 read asks for one max-size packet: the header is always
 * inside it, and its length is what tells the device how much payload to
 * expect. */
#define EP1_FIRST_READ USB_BULK_EP_MPS_FS

#define EP2_WR_BUF_SIZE 128
#define EP4_WR_BUF_SIZE 128

/*
 * EP4 is an interrupt IN endpoint and bInterval caps how often the host comes
 * to collect a report: 8 ms lets a 10 ms touch poll through at ~120 Hz; the
 * report is 8 bytes, so the periodic bandwidth is noise next to the EP1 image
 * stream.  Poll period and bInterval have to move together.
 */
#define EP4_POLL_INTERVAL_MS 8

static const uint8_t device_descriptor[] = {
	USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0, 0, 0, VENDOR_ID, PRODUCT_ID, 0,
	                           1),
};

/* One vendor interface, three endpoints: EP1 OUT (image stream), EP2 IN
 * (query replies), EP4 IN (touch reports). */
static const uint8_t config_descriptor[] = {
	USB_CONFIG_DESCRIPTOR_INIT((9 + 9 + 7 + 7 + 7), 1, 0x01,
	                           USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
	USB_INTERFACE_DESCRIPTOR_INIT(0, 0, 3, 0xFF, 0, 0, 0),
	USB_ENDPOINT_DESCRIPTOR_INIT(EP1_OUT_ADDR, USB_ENDPOINT_TYPE_BULK,
	                             USB_BULK_EP_MPS_FS, 0),
	USB_ENDPOINT_DESCRIPTOR_INIT(EP2_IN_ADDR, USB_ENDPOINT_TYPE_BULK,
	                             USB_BULK_EP_MPS_FS, 0),
	USB_ENDPOINT_DESCRIPTOR_INIT(EP4_IN_ADDR, USB_ENDPOINT_TYPE_INTERRUPT,
	                             64, EP4_POLL_INTERVAL_MS),
};

static const uint8_t device_quality_descriptor[] = {
	///////////////////////////////////////
	/// device qualifier descriptor
	///////////////////////////////////////
	0x0a, USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
	0x00, 0x02,
	0x00, 0x00,
	0x00, 0x40,
	0x00, 0x00,
};

static const char *string_descriptors[] = {
	(const char[]){ 0x09, 0x04 }, /* Langid */
	"embeddedboys", /* Manufacturer */
	"PUD Display", /* Product */
	"QD3503728-ESP32S3", /* Serial Number */
};

extern const struct usb_descriptor pud_vendor_descriptor;

struct usbd_interface *usbd_vendor_init_intf(uint8_t busid,
                                             struct usbd_interface *intf);

#ifdef __cplusplus
}
#endif

#endif /* _USBD_VENDOR_H_ */
