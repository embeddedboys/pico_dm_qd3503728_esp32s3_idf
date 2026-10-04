/*
 * USB device glue: event handling, the EP1 image-stream state machine with
 * slot-based flow control, the EP2 query FSM, EP4 touch push, and runtime
 * parameters.  ESP32-S3 port of Pico-USB-Display/src/cherryusb/usb.c.
 *
 * Platform deltas from the RP2350 original:
 *  - timebase is esp_timer (pud_millis()) instead of the Pico SDK;
 *  - a brightness parameter is *deferred* to the display task like the
 *    rotation: the backlight is a LEDC channel, and LEDC calls do not
 *    belong in the USB interrupt (on the RP2350 a PWM write is a single
 *    register store, so the original applies it right in the handler).
 *    Read-back semantics are unchanged (the requested value is cached).
 */

#include <string.h>

#include "esp_timer.h"

#include "pud.h"
#include "usb.h"
#include "decoder.h"
#include "backlight.h"

static inline u32 pud_millis(void)
{
	return (u32)(esp_timer_get_time() / 1000);
}

/* Set by the USB event handler: usbd_vendor_ep1_tick() must not arm anything
 * before the host has configured the interface (see the gate there). */
static volatile bool s_configured;

void usbd_event_handler(uint8_t busid, uint8_t event)
{
	switch (event) {
	case USBD_EVENT_RESET:
		s_configured = false;
		usbd_vendor_ep1_reset();
		break;
	case USBD_EVENT_CONNECTED:
		break;
	case USBD_EVENT_DISCONNECTED:
		/* No read survives the bus going away. */
		s_configured = false;
		usbd_vendor_ep1_reset();
		break;
	case USBD_EVENT_RESUME:
		break;
	case USBD_EVENT_SUSPEND:
		break;
	case USBD_EVENT_CONFIGURED:
		/* EP1 is open now: let the host start sending frames.  The
		 * configuration request is also what discards whatever the
		 * device endpoint had queued, so the framing state has to go
		 * first: with a stale s_ep1.armed, tick() believes a read is in
		 * flight, arms nothing, and every host bulk write then NAKs
		 * until the device is power-cycled. */
		s_configured = true;
		usbd_vendor_ep1_reset();
		usbd_vendor_ep1_tick();
		break;
	case USBD_EVENT_SET_REMOTE_WAKEUP:
		break;
	case USBD_EVENT_CLR_REMOTE_WAKEUP:
		break;
	default:
		break;
	}
}

extern u8 ep1_read_buffer[EP1_RD_BUF_SIZE];
extern u8 ep2_write_buffer[EP2_WR_BUF_SIZE];
extern u8 ep4_write_buffer[EP4_WR_BUF_SIZE];

/*
 * EP1 OUT, protocol v2: every transfer is a struct pud_ep1_header followed
 * by the payload it describes.  The first read asks for one max-size packet,
 * which always holds the whole header; the header's size then says exactly
 * how many more bytes belong to this transfer, so the end of a transfer
 * never depends on a short packet.
 *
 * Flow control: EP1 is armed only while a decoder slot is free -- an
 * un-armed endpoint NAKs, so the host's bulk write waits instead of the
 * frame being dropped.  Only touched from the USB ISR and the decoder task.
 */
static struct {
	volatile bool armed; /* a read is in flight */
	volatile uint32_t got; /* bytes of this transfer received */
	volatile uint32_t total; /* header + payload; 0 until parsed */
	volatile uint32_t start_ms; /* when the read last made progress */
	struct pud_ep1_header hdr;
} s_ep1;

/*
 * How long a transfer may go without a single byte before it is thrown away.
 * A legal transfer takes tens of ms at the link's ~1 MB/s, but it may begin
 * at any time after the read was armed, so the deadline follows progress,
 * not the arming.  A host that has gone away sends nothing at all, so it
 * still trips this.
 */
#define EP1_TRANSFER_MAX_MS 500

/* Stamp the last progress: arming the endpoint, and every byte that
 * arrives. */
static void ep1_mark(void)
{
	s_ep1.start_ms = pud_millis();
}

/* Diagnostics, readable from a debugger; all stay 0 with a matching host. */
volatile struct {
	u32 oversize; /* declared more than ep1_read_buffer holds */
	u32 bad; /* no usable header, or a length mismatch */
	u32 stale; /* an incomplete transfer was dropped */
	u32 sink_frames; /* PUD_EP1_SINK: transfers accepted and discarded */
	u32 sink_bytes; /* PUD_EP1_SINK: their payload bytes */
} g_ep1_stat;

void usbd_vendor_ep1_reset(void)
{
	s_ep1.armed = false;
	s_ep1.got = 0;
	s_ep1.total = 0;
	ep1_mark();
}

/* Called by the decoder task after releasing a frame slot, and once the
 * device is configured. */
void usbd_vendor_ep1_tick(void)
{
	/* Nothing is open before the host configures the interface, and arming
	 * an endpoint that early corrupts its state. */
	if (!usb_is_configured())
		return;

	if (s_ep1.armed || !decoder_slot_free())
		return;

	s_ep1.armed = true;
	s_ep1.got = 0;
	s_ep1.total = 0;
	ep1_mark();
	usbd_ep_start_read(0, EP1_OUT_ADDR, ep1_read_buffer, EP1_FIRST_READ);
}

/* Ask for whatever is still missing from this transfer.  The header packet
 * already put its bytes in place, so the payload stays contiguous behind
 * it. */
static void ep1_read_more(void)
{
	uint32_t want = s_ep1.total ? s_ep1.total - s_ep1.got : EP1_FIRST_READ;

	s_ep1.armed = true;
	usbd_ep_start_read(0, EP1_OUT_ADDR, ep1_read_buffer + s_ep1.got, want);
}

/*
 * Caretaker for EP1, called from the decoder task while it waits for work.
 *
 * A host that goes away mid-transfer would otherwise leave the endpoint
 * armed forever: the next header would be read as the rest of that payload.
 * A transfer that makes no progress for longer than any packet should take
 * is thrown away and the endpoint armed again.  A live transfer refreshes
 * the deadline with every packet, so only a silent host can trip it.
 */
void usbd_vendor_ep1_poll(void)
{
	uint32_t now = pud_millis();

	if (s_ep1.armed && (s_ep1.total || s_ep1.got) &&
	    (uint32_t)(now - s_ep1.start_ms) > EP1_TRANSFER_MAX_MS) {
		g_ep1_stat.stale++;
		usbd_vendor_ep1_reset();
		usbd_vendor_ep1_tick();
	}
}

/* One transfer is over: either hand the payload to the decoder task or just
 * take the next one. */
static void ep1_finish(bool submit)
{
	uint16_t xs = s_ep1.hdr.xs, ys = s_ep1.hdr.ys;
	uint16_t xe = s_ep1.hdr.xe, ye = s_ep1.hdr.ye;
	uint32_t size = s_ep1.hdr.size;

	s_ep1.got = 0;
	s_ep1.total = 0;

	if (submit && size) {
		/* Do not decode here: this runs on the USB interrupt stack. */
		decoder_submit_frame(xs, ys, xe, ye,
		                     ep1_read_buffer + PUD_EP1_HEADER_SIZE,
		                     size);
		/* The payload was copied into a frame slot, so ep1_read_buffer
		 * is free again: arm the next transfer right away if a slot is
		 * still free.  Waiting for the decoder task instead would leave
		 * EP1 un-armed while the host is already writing the next band,
		 * and a NAK on a full-speed bulk pipe costs a whole frame. */
	}

	usbd_vendor_ep1_tick();
}

void usbd_vendor_ep1_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
	(void)busid;
	(void)ep;

	s_ep1.armed = false;

	if (!nbytes) {
		/* A zero-length packet ends a transfer with nothing in it. */
		ep1_finish(false);
		return;
	}

	s_ep1.got += nbytes;
	ep1_mark();

	if (!s_ep1.total) {
		/* still reading the packet that carries the header */
		if (s_ep1.got < PUD_EP1_HEADER_SIZE) {
			if (nbytes < EP1_FIRST_READ) {
				/* short packet: the transfer is over and
				 * never held a whole header */
				g_ep1_stat.bad++;
				ep1_finish(false);
			} else {
				ep1_read_more();
			}
			return;
		}

		memcpy(&s_ep1.hdr, ep1_read_buffer, sizeof(s_ep1.hdr));
		s_ep1.total = PUD_EP1_HEADER_SIZE + s_ep1.hdr.size;

		/*
		 * A header is only believable if it describes a rectangle on
		 * the panel and a payload that fits.  Anything else means the
		 * stream lost its framing, and the transfer has to go so the
		 * next header is parsed from a clean slate.  Stalling EP1 here
		 * instead fails the host's write, and a host that then clears
		 * the halt and retries has been measured to wedge the host
		 * controller -- so the transfer is dropped, counted, and the
		 * endpoint re-armed.
		 *
		 * An odd `size` is *not* rejected (measured on the RP2350
		 * build): the QOI decoder stops at its end marker, so a host
		 * that pads to even decodes identically.
		 */
		if (s_ep1.total > EP1_RD_BUF_SIZE) {
			g_ep1_stat.oversize++;
			ep1_finish(false);
			return;
		}

		if (s_ep1.hdr.xs > s_ep1.hdr.xe ||
		    s_ep1.hdr.ys > s_ep1.hdr.ye ||
		    s_ep1.hdr.xe >= g_pud_data.disp.xres ||
		    s_ep1.hdr.ye >= g_pud_data.disp.yres) {
			g_ep1_stat.bad++;
			ep1_finish(false);
			return;
		}
	}

	if (s_ep1.got < s_ep1.total) {
		ep1_read_more();
		return;
	}

	if (s_ep1.got != s_ep1.total) {
		/* the host sent more than it declared */
		g_ep1_stat.bad++;
		ep1_finish(false);
		return;
	}

	ep1_finish(true);
}

/* Runtime parameters (PUD_CMD_SET_PARAM / PUD_CMD_GET_PARAM).
 *
 * The values are read back from whoever owns them instead of being cached
 * here, so a query can never report a stale copy of something that changed
 * behind its back.  What is not implemented is named instead of pretended:
 * see PUD_PARAMS_SETTABLE below.
 */
#define PUD_PARAMS_SETTABLE (PUD_PARAM_BRIGHTNESS | PUD_PARAM_ROTATION)

/* Bits of the last PUD_CMD_SET_PARAM that could not be applied. */
static u32 s_params_rejected;

/* The brightness the host asked for.  Cached for read-back, and applied to
 * the LEDC channel by the display task (see the file-top comment). */
static u8 s_brightness;
static bool s_brightness_set;

/* Rotating the panel is one MADCTL write plus the geometry and touch
 * transform that go with it.  The bookkeeping happens right here, because
 * the host reads the capability report back to size its mode and has to see
 * the new geometry immediately; the register write waits for the display
 * task, because that goes out over the panel bus and this runs in the USB
 * interrupt. */
static u8 s_panel_rotation = TFT_ROTATION; /* what the panel is programmed for */
static volatile u8 s_want_rotation = TFT_ROTATION;
static volatile bool s_want_brightness;

void pud_params_read(struct pud_param_state *st)
{
	st->settable = PUD_PARAMS_SETTABLE;
	st->rejected = s_params_rejected;
	st->brightness = s_brightness_set ? s_brightness : backlight_get_level();
	st->rotation = g_pud_data.disp.rotation;
	st->reserved = 0;
	st->decoder = DECODER_TYPE;
}

void pud_params_apply(const struct pud_params *p)
{
	u32 applied = 0;

	if (p->mask & PUD_PARAM_BRIGHTNESS) {
		u8 level = p->brightness > 100 ? 100 : p->brightness;

		s_brightness = level;
		s_brightness_set = true;
		s_want_brightness = true;
		applied |= PUD_PARAM_BRIGHTNESS;
	}

	if ((p->mask & PUD_PARAM_ROTATION) && p->rotation <= TFT_ROTATE_270) {
		u8 rot = p->rotation;

		g_pud_data.disp.rotation = rot;
		/* The frame the panel is driven in follows the rotation, and
		 * the host builds its mode from what it reads back here. */
		if (rot & 1) {
			g_pud_data.disp.xres = TFT_VER_RES;
			g_pud_data.disp.yres = TFT_HOR_RES;
		} else {
			g_pud_data.disp.xres = TFT_HOR_RES;
			g_pud_data.disp.yres = TFT_VER_RES;
		}

		/* touch follows the display; this is pure bookkeeping too */
		if (!INDEV_DRV_NOT_USED)
			indev_set_dir(indev_dir_for_rotation(rot));

		s_want_rotation = rot;
		applied |= PUD_PARAM_ROTATION;
	}

	/* What is left is a build-time choice: DECODER_TYPE selects which
	 * decoders are compiled in. */
	s_params_rejected = p->mask & ~applied;
}

/*
 * Put what the host asked for on the hardware.  Called by the display task
 * (the panel's only writer) before it draws anything, returns true when it
 * changed something.
 */
bool pud_params_flush_display(void)
{
	bool changed = false;
	u8 rot = s_want_rotation;

	if (s_want_brightness) {
		s_want_brightness = false;
		backlight_set_level(s_brightness);
		changed = true;
	}

	if (rot != s_panel_rotation) {
		if (tft_set_rotation(rot) == 0) {
			s_panel_rotation = rot;
			changed = true;
		}
	}

	return changed;
}

/* Fills ep2_write_buffer for one query and returns how many bytes to send.
 * The caller writes exactly that many, so a command that produces less than
 * the host asked for cannot leak stale buffer contents past its payload. */
uint32_t usbd_vendor_ep2_bulk_in_fsm(uint8_t cmd, uint32_t len)
{
	switch (cmd) {
	case PUD_CMD_GET_SN:
		pud_get_ro_sn(ep2_write_buffer, len);
		break;
	case PUD_CMD_GET_CAPS: {
		const struct pud_caps caps = {
			.magic = PUD_CAPS_MAGIC,
			.proto_ver = PUD_PROTO_VER,
			.frame_max = PUD_MAX_TRANSFER,
			.decoder_type = DECODER_TYPE,
			.xres = g_pud_data.disp.xres,
			.yres = g_pud_data.disp.yres,
			.pixelclock_khz = g_pud_data.disp.pixelclock_khz,
			.rotation = g_pud_data.disp.rotation,
			.bpp = g_pud_data.disp.bpp,
			.intf_type = g_pud_data.disp.intf_type,
			.tp_polling_period =
			        INDEV_DRV_NOT_USED ?
			                0 :
			                g_pud_data.tp.polling_period,
			.width_mm = g_pud_data.disp.width_mm,
			.height_mm = g_pud_data.disp.height_mm,
			/* the host uses this to decide whether to register an
			 * input device at all */
			.flags = INDEV_DRV_NOT_USED ? 0 : PUD_CAPS_TOUCH,
		};

		if (len > sizeof(caps))
			len = sizeof(caps);
		memcpy(ep2_write_buffer, &caps, len);
		break;
	}
	case PUD_CMD_GET_QOID: {
		struct pud_qoid_state st;

		qoid_read_state(&st);
		if (len > sizeof(st))
			len = sizeof(st);
		memcpy(ep2_write_buffer, &st, len);
		break;
	}
	case PUD_CMD_GET_PARAM: {
		struct pud_param_state st;

		pud_params_read(&st);
		if (len > sizeof(st))
			len = sizeof(st);
		memcpy(ep2_write_buffer, &st, len);
		break;
	}
	default:
		/* Answer with nothing rather than the host's requested size:
		 * ep2_write_buffer still holds the previous answer, so replying
		 * with stale bytes makes an unknown command look like a valid
		 * response.  A zero-length write makes the host's bulk read
		 * come back short, which is what it should treat as
		 * unsupported. */
		len = 0;
		break;
	}

	return len;
}

/*
 * EP2 IN completion.  The reply was handed to the endpoint in
 * vendor_request_handler(); the callback exists because CherryUSB needs one
 * per endpoint.
 */
void usbd_vendor_ep2_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
	(void)busid;
	(void)ep;
	(void)nbytes;
}

/*
 * EP4: touch reports.
 *
 * The device pushes one report per touch poll and the host keeps an
 * interrupt URB pending, so there is no per-sample handshake that can be
 * lost.  A report that arrives while the previous one is still in flight
 * replaces it: the host only needs the latest state.
 */
static struct pud_touch_report s_ep4_report;
static volatile bool s_ep4_busy; /* transfer in flight */
static volatile bool s_ep4_dirty; /* a newer report arrived meanwhile */

static int usbd_vendor_ep4_arm(void)
{
	int rc;

	memcpy(ep4_write_buffer, &s_ep4_report, sizeof(s_ep4_report));
	s_ep4_dirty = false;
	s_ep4_busy = true;

	rc = usbd_ep_start_write(0, EP4_IN_ADDR, ep4_write_buffer,
	                         sizeof(s_ep4_report));
	if (rc < 0)
		s_ep4_busy = false;

	return rc;
}

int usbd_vendor_ep4_submit(const struct pud_touch_report *report)
{
	if (report == NULL)
		return -1;

	s_ep4_report = *report;

	if (s_ep4_busy) {
		/* the host has not taken the previous one yet; the next
		 * completion re-arms with this one */
		s_ep4_dirty = true;
		return 0;
	}

	return usbd_vendor_ep4_arm();
}

int usbd_vendor_ep4_request(void)
{
	if (s_ep4_busy)
		return 0; /* already on its way to the host */

	return usbd_vendor_ep4_arm();
}

void usbd_vendor_ep4_reset(void)
{
	s_ep4_busy = false;
	s_ep4_dirty = false;
	memset(&s_ep4_report, 0, sizeof(s_ep4_report));
	/* version 0 tells a polling host that this build has no touch driver;
	 * the capability reply carries the same fact as PUD_CAPS_TOUCH */
	s_ep4_report.version = INDEV_DRV_NOT_USED ? 0 : PUD_TOUCH_VERSION;
}

void usbd_vendor_ep4_int_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
	(void)busid;
	(void)ep;
	(void)nbytes;

	s_ep4_busy = false;

	/* a sample arrived while this one was in flight: send the newer one */
	if (s_ep4_dirty)
		usbd_vendor_ep4_arm();
}

struct usbd_endpoint vendor_out_ep1 = {
	.ep_addr = EP1_OUT_ADDR,
	.ep_cb = usbd_vendor_ep1_bulk_out,
};

struct usbd_endpoint vendor_in_ep2 = {
	.ep_addr = EP2_IN_ADDR,
	.ep_cb = usbd_vendor_ep2_bulk_in,
};

struct usbd_endpoint vendor_in_ep4 = {
	.ep_addr = EP4_IN_ADDR,
	.ep_cb = usbd_vendor_ep4_int_in,
};

struct usbd_interface intf0;

void usb_device_init()
{
	/* so a host can tell "touch is implemented" (version != 0) before
	 * anybody has touched the panel */
	usbd_vendor_ep4_reset();

	usbd_desc_register(0, &pud_vendor_descriptor);

	usbd_add_interface(0, usbd_vendor_init_intf(0, &intf0));

	usbd_add_endpoint(0, &vendor_out_ep1);
	usbd_add_endpoint(0, &vendor_in_ep2);
	usbd_add_endpoint(0, &vendor_in_ep4);

	usbd_initialize(0, ESP_USBD_BASE, usbd_event_handler);
}

bool usb_is_configured(void)
{
	return s_configured;
}
