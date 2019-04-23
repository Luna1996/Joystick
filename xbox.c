#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/usb/input.h>
#include <linux/usb/quirks.h>

#define XPAD_PKT_LEN 64

#define MAP_DPAD_TO_BUTTONS (1 << 0)
#define MAP_TRIGGERS_TO_BUTTONS (1 << 1)
#define MAP_STICKS_TO_NULL (1 << 2)
#define DANCEPAD_MAP_CONFIG \
  (MAP_DPAD_TO_BUTTONS | MAP_TRIGGERS_TO_BUTTONS | MAP_STICKS_TO_NULL)

static const struct xpad_device {
  u16 idVendor;
  u16 idProduct;
  char *name;
  u8 mapping;
  u8 xtype;
} xpad_device[] = {{0x045e, 0x028e, "XBOX 360 PAD - CSE 522S", 0, 1}};

/* buttons shared with xbox and xbox360 */
static const signed short xpad_common_btn[] = {
    BTN_A,     BTN_B,      BTN_X,      BTN_Y,      /* "analog" buttons */
    BTN_START, BTN_SELECT, BTN_THUMBL, BTN_THUMBR, /* start/back/sticks */
    -1                                             /* terminating entry */
};

/* original xbox controllers only */
static const signed short xpad_btn[] = {
    BTN_C, BTN_Z, /* "analog" buttons */
    -1            /* terminating entry */
};

/* used when dpad is mapped to buttons */
static const signed short xpad_btn_pad[] = {
    BTN_TRIGGER_HAPPY1, BTN_TRIGGER_HAPPY2, /* d-pad left, right */
    BTN_TRIGGER_HAPPY3, BTN_TRIGGER_HAPPY4, /* d-pad up, down */
    -1                                      /* terminating entry */
};

/* used when triggers are mapped to buttons */
static const signed short xpad_btn_triggers[] = {
    BTN_TL2, BTN_TR2, /* triggers left/right */
    -1};

static const signed short xpad360_btn[] = {/* buttons for x360 controller */
                                           BTN_TL, BTN_TR, /* Button LB/RB */
                                           BTN_MODE, /* The big X button */
                                           -1};

static const signed short xpad_abs[] = {
    ABS_X, ABS_Y,   /* left stick */
    ABS_RX, ABS_RY, /* right stick */
    -1              /* terminating entry */
};

/* used when dpad is mapped to axes */
static const signed short xpad_abs_pad[] = {
    ABS_HAT0X, ABS_HAT0Y, /* d-pad axes */
    -1                    /* terminating entry */
};

/* used when triggers are mapped to axes */
static const signed short xpad_abs_triggers[] = {
    ABS_Z, ABS_RZ, /* triggers left/right */
    -1};

/*
 * Xbox 360 has a vendor-specific class, so we cannot match it with only
 * USB_INTERFACE_INFO (also specifically refused by USB subsystem), so we
 * match against vendor id as well. Wired Xbox 360 devices have protocol 1,
 * wireless controllers have protocol 129.
 */
#define XPAD_XBOX360_VENDOR_PROTOCOL(vend, pr)                              \
  .match_flags = USB_DEVICE_ID_MATCH_VENDOR | USB_DEVICE_ID_MATCH_INT_INFO, \
  .idVendor = (vend), .bInterfaceClass = USB_CLASS_VENDOR_SPEC,             \
  .bInterfaceSubClass = 93, .bInterfaceProtocol = (pr)
#define XPAD_XBOX360_VENDOR(vend)              \
  {XPAD_XBOX360_VENDOR_PROTOCOL((vend), 1)}, { \
    XPAD_XBOX360_VENDOR_PROTOCOL((vend), 129)  \
  }

struct xpad_output_packet {
  u8 data[XPAD_PKT_LEN];
  u8 len;
  bool pending;
};

#define XPAD_OUT_CMD_IDX 0
#define XPAD_OUT_FF_IDX 1
#define XPAD_OUT_LED_IDX (1 + IS_ENABLED(CONFIG_JOYSTICK_XPAD_FF))
#define XPAD_NUM_OUT_PACKETS                 \
  (1 + IS_ENABLED(CONFIG_JOYSTICK_XPAD_FF) + \
   IS_ENABLED(CONFIG_JOYSTICK_XPAD_LEDS))

struct usb_xpad {
  struct input_dev *dev; /* input device interface */
  struct input_dev __rcu *x360w_dev;
  struct usb_device *udev;    /* usb device */
  struct usb_interface *intf; /* usb interface */

  bool pad_present;
  bool input_created;

  struct urb *irq_in;   /* urb for interrupt in report */
  unsigned char *idata; /* input data */
  dma_addr_t idata_dma;

  struct urb *irq_out; /* urb for interrupt out report */
  struct usb_anchor irq_out_anchor;
  bool irq_out_active;  /* we must not use an active URB */
  u8 odata_serial;      /* serial number for xbox one protocol */
  unsigned char *odata; /* output data */
  dma_addr_t odata_dma;
  spinlock_t odata_lock;

  struct xpad_output_packet out_packets[XPAD_NUM_OUT_PACKETS];
  int last_out_packet;
  int init_seq;

  char phys[64]; /* physical device path */

  int mapping;             /* map d-pad to buttons or to axes */
  int xtype;               /* type of xbox device */
  int pad_nr;              /* the order x360 pads were attached */
  const char *name;        /* name of the device */
  struct work_struct work; /* init/remove device from callback */
};

static int xpad_init_input(struct usb_xpad *xpad);
static void xpad_deinit_input(struct usb_xpad *xpad);

/*
 *	xpad360_process_packet
 *
 *	Completes a request by converting the data into events for the
 *	input subsystem. It is version for xbox 360 controller
 *
 *	The used report descriptor was taken from:
 *		http://www.free60.org/wiki/Gamepad
 */

static void xpad360_process_packet(struct usb_xpad *xpad, struct input_dev *dev,
                                   u16 cmd, unsigned char *data) {
  /* valid pad data */
  if (data[0] != 0x00) return;

  /* digital pad */
  if (xpad->mapping & MAP_DPAD_TO_BUTTONS) {
    /* dpad as buttons (left, right, up, down) */
    input_report_key(dev, BTN_TRIGGER_HAPPY1, data[2] & 0x04);
    input_report_key(dev, BTN_TRIGGER_HAPPY2, data[2] & 0x08);
    input_report_key(dev, BTN_TRIGGER_HAPPY3, data[2] & 0x01);
    input_report_key(dev, BTN_TRIGGER_HAPPY4, data[2] & 0x02);
  }

  /*
   * This should be a simple else block. However historically
   * xbox360w has mapped DPAD to buttons while xbox360 did not. This
   * made no sense, but now we can not just switch back and have to
   * support both behaviors.
   */
  input_report_abs(dev, ABS_HAT0X, !!(data[2] & 0x08) - !!(data[2] & 0x04));
  input_report_abs(dev, ABS_HAT0Y, !!(data[2] & 0x02) - !!(data[2] & 0x01));

  /* start/back buttons */
  input_report_key(dev, BTN_START, data[2] & 0x10);
  input_report_key(dev, BTN_SELECT, data[2] & 0x20);

  /* stick press left/right */
  input_report_key(dev, BTN_THUMBL, data[2] & 0x40);
  input_report_key(dev, BTN_THUMBR, data[2] & 0x80);

  /* buttons A,B,X,Y,TL,TR and MODE */
  input_report_key(dev, BTN_A, data[3] & 0x10);
  input_report_key(dev, BTN_B, data[3] & 0x20);
  input_report_key(dev, BTN_X, data[3] & 0x40);
  input_report_key(dev, BTN_Y, data[3] & 0x80);
  input_report_key(dev, BTN_TL, data[3] & 0x01);
  input_report_key(dev, BTN_TR, data[3] & 0x02);
  input_report_key(dev, BTN_MODE, data[3] & 0x04);

  if (!(xpad->mapping & MAP_STICKS_TO_NULL)) {
    /* left stick */
    input_report_abs(dev, ABS_X, (__s16)le16_to_cpup((__le16 *)(data + 6)));
    input_report_abs(dev, ABS_Y, ~(__s16)le16_to_cpup((__le16 *)(data + 8)));

    /* right stick */
    input_report_abs(dev, ABS_RX, (__s16)le16_to_cpup((__le16 *)(data + 10)));
    input_report_abs(dev, ABS_RY, ~(__s16)le16_to_cpup((__le16 *)(data + 12)));
  }

  /* triggers left/right */
  if (xpad->mapping & MAP_TRIGGERS_TO_BUTTONS) {
    input_report_key(dev, BTN_TL2, data[4]);
    input_report_key(dev, BTN_TR2, data[5]);
  } else {
    input_report_abs(dev, ABS_Z, data[4]);
    input_report_abs(dev, ABS_RZ, data[5]);
  }

  input_sync(dev);
}

static void xpad_presence_work(struct work_struct *work) {
  struct usb_xpad *xpad = container_of(work, struct usb_xpad, work);
  int error;

  if (xpad->pad_present) {
    error = xpad_init_input(xpad);
    if (error) {
      /* complain only, not much else we can do here */
      dev_err(&xpad->dev->dev, "unable to init device: %d\n", error);
    } else {
      rcu_assign_pointer(xpad->x360w_dev, xpad->dev);
    }
  } else {
    RCU_INIT_POINTER(xpad->x360w_dev, NULL);
    synchronize_rcu();
    /*
     * Now that we are sure xpad360w_process_packet is not
     * using input device we can get rid of it.
     */
    xpad_deinit_input(xpad);
  }
}

static void xpad_irq_in(struct urb *urb) {
  struct usb_xpad *xpad = urb->context;
  struct device *dev = &xpad->intf->dev;
  int retval, status;

  status = urb->status;

  switch (status) {
    case 0:
      /* success */
      break;
    case -ECONNRESET:
    case -ENOENT:
    case -ESHUTDOWN:
      /* this urb is terminated, clean up */
      dev_dbg(dev, "%s - urb shutting down with status: %d\n", __func__,
              status);
      return;
    default:
      dev_dbg(dev, "%s - nonzero urb status received: %d\n", __func__, status);
      goto exit;
  }

  xpad360_process_packet(xpad, xpad->dev, 0, xpad->idata);

exit:
  retval = usb_submit_urb(urb, GFP_ATOMIC);
  if (retval)
    dev_err(dev, "%s - usb_submit_urb failed with result %d\n", __func__,
            retval);
}

static int xpad_start_input(struct usb_xpad *xpad) {
  if (usb_submit_urb(xpad->irq_in, GFP_KERNEL)) return -EIO;
  return 0;
}

static void xpad_stop_input(struct usb_xpad *xpad) {
  usb_kill_urb(xpad->irq_in);
}

static int xpad_open(struct input_dev *dev) {
  struct usb_xpad *xpad = input_get_drvdata(dev);

  return xpad_start_input(xpad);
}

static void xpad_close(struct input_dev *dev) {
  struct usb_xpad *xpad = input_get_drvdata(dev);

  xpad_stop_input(xpad);
}

static void xpad_set_up_abs(struct input_dev *input_dev, signed short abs) {
  switch (abs) {
    case ABS_X:
    case ABS_Y:
    case ABS_RX:
    case ABS_RY:
      input_set_abs_params(input_dev, abs, -32768, 32767, 16, 128);
      break;
    case ABS_Z:
    case ABS_RZ:
      input_set_abs_params(input_dev, abs, 0, 255, 0, 0);
      break;
    case ABS_HAT0X:
    case ABS_HAT0Y:
      input_set_abs_params(input_dev, abs, -1, 1, 0, 0);
      break;
    default:
      input_set_abs_params(input_dev, abs, 0, 0, 0, 0);
      break;
  }
}

static void xpad_deinit_input(struct usb_xpad *xpad) {
  if (xpad->input_created) {
    xpad->input_created = false;
    input_unregister_device(xpad->dev);
  }
}

static int xpad_init_input(struct usb_xpad *xpad) {
  struct input_dev *input_dev;
  int i, error;

  input_dev = input_allocate_device();
  if (!input_dev) return -ENOMEM;

  xpad->dev = input_dev;
  input_dev->name = xpad->name;
  input_dev->phys = xpad->phys;
  usb_to_input_id(xpad->udev, &input_dev->id);

  input_dev->dev.parent = &xpad->intf->dev;

  input_set_drvdata(input_dev, xpad);

  input_dev->open = xpad_open;
  input_dev->close = xpad_close;

  for (i = 0; xpad_abs[i] >= 0; i++) xpad_set_up_abs(input_dev, xpad_abs[i]);

  for (i = 0; xpad_common_btn[i] >= 0; i++)
    input_set_capability(input_dev, EV_KEY, xpad_common_btn[i]);

  for (i = 0; xpad360_btn[i] >= 0; i++)
    input_set_capability(input_dev, EV_KEY, xpad360_btn[i]);

  for (i = 0; xpad_abs_pad[i] >= 0; i++)
    xpad_set_up_abs(input_dev, xpad_abs_pad[i]);

  for (i = 0; xpad_abs_triggers[i] >= 0; i++)
    xpad_set_up_abs(input_dev, xpad_abs_triggers[i]);

  error = input_register_device(xpad->dev);
  xpad->input_created = true;
  return 0;
}

static int xpad_probe(struct usb_interface *intf,
                      const struct usb_device_id *id) {
  struct usb_device *udev = interface_to_usbdev(intf);
  struct usb_xpad *xpad;
  struct usb_endpoint_descriptor *ep_irq_in, *ep_irq_out;
  int i, error;

  xpad = kzalloc(sizeof(struct usb_xpad), GFP_KERNEL);

  usb_make_path(udev, xpad->phys, sizeof(xpad->phys));
  strlcat(xpad->phys, "/input0", sizeof(xpad->phys));

  xpad->idata =
      usb_alloc_coherent(udev, XPAD_PKT_LEN, GFP_KERNEL, &xpad->idata_dma);

  xpad->irq_in = usb_alloc_urb(0, GFP_KERNEL);

  xpad->udev = udev;
  xpad->intf = intf;
  xpad->mapping = xpad_device[0].mapping;
  xpad->xtype = xpad_device[0].xtype;
  xpad->name = xpad_device[0].name;
  INIT_WORK(&xpad->work, xpad_presence_work);

  ep_irq_in = ep_irq_out = NULL;

  for (i = 0; i < 2; i++) {
    struct usb_endpoint_descriptor *ep =
        &intf->cur_altsetting->endpoint[i].desc;

    if (usb_endpoint_xfer_int(ep)) {
      if (usb_endpoint_dir_in(ep))
        ep_irq_in = ep;
      else
        ep_irq_out = ep;
    }
  }

  usb_fill_int_urb(
      xpad->irq_in, udev, usb_rcvintpipe(udev, ep_irq_in->bEndpointAddress),
      xpad->idata, XPAD_PKT_LEN, xpad_irq_in, xpad, ep_irq_in->bInterval);
  xpad->irq_in->transfer_dma = xpad->idata_dma;
  xpad->irq_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

  usb_set_intfdata(intf, xpad);

  error = xpad_init_input(xpad);
  return 0;
}

static void xpad_disconnect(struct usb_interface *intf) {
  struct usb_xpad *xpad = usb_get_intfdata(intf);
  xpad_deinit_input(xpad);
  usb_free_urb(xpad->irq_in);
  usb_free_coherent(xpad->udev, XPAD_PKT_LEN, xpad->idata, xpad->idata_dma);
  kfree(xpad);
  usb_set_intfdata(intf, NULL);
}

static const struct usb_device_id xpad_table[] = {XPAD_XBOX360_VENDOR(0x045e),
                                                  {}};

MODULE_DEVICE_TABLE(usb, xpad_table);

static struct usb_driver xpad_driver = {
    .name = "xpad",
    .probe = xpad_probe,
    .disconnect = xpad_disconnect,
    .id_table = xpad_table,
};

module_usb_driver(xpad_driver);
MODULE_LICENSE("GPL");