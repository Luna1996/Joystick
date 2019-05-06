#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/usb/input.h>
#include <linux/usb/quirks.h>

#define XPAD_PKT_LEN 64

struct usb_xpad {
  struct input_dev *dev;      /* input device interface */
  struct usb_device *udev;    /* usb device */
  struct usb_interface *intf; /* usb interface */

  bool input_created;

  struct urb *irq_in;   /* urb for interrupt in report */
  unsigned char *idata; /* input data */
  dma_addr_t idata_dma;

  char phys[64]; /* physical device path */

  const char *name; /* name of the device */
};

#define DPAD_SHIFT_LEFT 0x01
#define DPAD_SHIFT_RIGHT 0x02
#define DPAD_SHIFT_UP 0x04
#define DPAD_SHIFT_DONW 0x08
#define START_SHIFT 0x10
#define SELECT_SHIFT 0x20
#define LT_SHIFT 0x40
#define RT_SHIFT 0x80
#define BUTTON_A_SHIFT 0x10
#define BUTTON_B_SHIFT 0x20
#define BUTTON_X_SHIFT 0x40
#define BUTTON_Y_SHIFT 0x80
#define BUTTON_TL_SHIFT 0x01
#define BUTTON_TR_SHIFT 0x02
#define BUTTON_MODE_SHIFT 0x04

static void xpad_irq_in(struct urb *urb) {
  struct usb_xpad *xpad = urb->context;
  struct input_dev *dev = xpad->dev;
  unsigned char *data = xpad->idata;
  int retval, status, i;

  status = urb->status;

  switch (status) {
    case 0:
      /* success */
      if (data[0] != 0x00) break;

      /* debug */
      printk("xbox-debug:-----------------------\n");
      for (i = 0; i < 8; i++) {
        printk("xbox-debug:%02x %02x %02x %02x %02x %02x %02x %02x\n",
               data[i * 8 + 0], data[i * 8 + 1], data[i * 8 + 2],
               data[i * 8 + 3], data[i * 8 + 4], data[i * 8 + 5],
               data[i * 8 + 6], data[i * 8 + 7], );
      }

      /* D-pad axis*/
      input_report_abs(
          dev, ABS_HAT0X,
          !!(data[2] & DPAD_SHIFT_DONW) - !!(data[2] & DPAD_SHIFT_UP));
      input_report_abs(
          dev, ABS_HAT0Y,
          !!(data[2] & DPAD_SHIFT_RIGHT) - !!(data[2] & DPAD_SHIFT_LEFT));
      /* start/back buttons */
      input_report_key(dev, BTN_START, data[2] & START_SHIFT);
      input_report_key(dev, BTN_SELECT, data[2] & SELECT_SHIFT);
      /* stick press left/right */
      input_report_key(dev, BTN_THUMBL, data[2] & LT_SHIFT);
      input_report_key(dev, BTN_THUMBR, data[2] & RT_SHIFT);
      /* buttons A,B,X,Y,TL,TR and MODE */
      input_report_key(dev, BTN_A, data[3] & BUTTON_A_SHIFT);
      input_report_key(dev, BTN_B, data[3] & BUTTON_B_SHIFT);
      input_report_key(dev, BTN_X, data[3] & BUTTON_X_SHIFT);
      input_report_key(dev, BTN_Y, data[3] & BUTTON_Y_SHIFT);
      input_report_key(dev, BTN_TL, data[3] & BUTTON_TL_SHIFT);
      input_report_key(dev, BTN_TR, data[3] & BUTTON_TR_SHIFT);
      input_report_key(dev, BTN_MODE, data[3] & BUTTON_MODE_SHIFT);
      /* left stick */
      input_report_abs(dev, ABS_X, (__s16)le16_to_cpup((__le16 *)(data + 6)));
      input_report_abs(dev, ABS_Y, ~(__s16)le16_to_cpup((__le16 *)(data + 8)));

      /* right stick */
      input_report_abs(dev, ABS_RX, (__s16)le16_to_cpup((__le16 *)(data + 10)));
      input_report_abs(dev, ABS_RY,
                       ~(__s16)le16_to_cpup((__le16 *)(data + 12)));

      /* triggers left/right */
      input_report_abs(dev, ABS_Z, data[4]);
      input_report_abs(dev, ABS_RZ, data[5]);

      input_sync(dev);
      break;
    case -ECONNRESET:
    case -ENOENT:
    case -ESHUTDOWN:
      /* this urb is terminated, clean up */
      return;
    default:
      return;
  }

  retval = usb_submit_urb(urb, GFP_ATOMIC);
}

static int xpad_open(struct input_dev *dev) {
  struct usb_xpad *xpad = input_get_drvdata(dev);
  if (usb_submit_urb(xpad->irq_in, GFP_KERNEL)) return -EIO;
  return 0;
}

static void xpad_close(struct input_dev *dev) {
  struct usb_xpad *xpad = input_get_drvdata(dev);
  usb_kill_urb(xpad->irq_in);
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
  }
}

/* buttons shared with xbox and xbox360 */
static const signed short xpad_common_btn[] = {
    BTN_A,     BTN_B,      BTN_X,      BTN_Y,      /* "analog" buttons */
    BTN_START, BTN_SELECT, BTN_THUMBL, BTN_THUMBR, /* start/back/sticks */
    BTN_TL,    BTN_TR,                             /* Button LB/RB */
    BTN_MODE                                       /* The big X button */
};

static const signed short xpad_abs[] = {
    ABS_X,     ABS_Y,     /* left stick */
    ABS_RX,    ABS_RY,    /* right stick */
    ABS_HAT0X, ABS_HAT0Y, /* d-pad axes */
    ABS_Z,     ABS_RZ     /* triggers left/right */
};

static int xpad_init_input(struct usb_xpad *xpad) {
  struct input_dev *input_dev;
  int i, error;

  input_dev = input_allocate_device();

  xpad->dev = input_dev;
  input_dev->name = xpad->name;
  input_dev->phys = xpad->phys;
  usb_to_input_id(xpad->udev, &input_dev->id);

  input_dev->dev.parent = &xpad->intf->dev;

  input_set_drvdata(input_dev, xpad);

  input_dev->open = xpad_open;
  input_dev->close = xpad_close;

  for (i = 0; i < 8; i++) xpad_set_up_abs(input_dev, xpad_abs[i]);

  for (i = 0; i < 11; i++)
    input_set_capability(input_dev, EV_KEY, xpad_common_btn[i]);

  error = input_register_device(input_dev);

  xpad->input_created = true;
  return 0;
}

static int xpad_probe(struct usb_interface *intf,
                      const struct usb_device_id *id) {
  printk("xbox-debug:Device detected!");
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
  xpad->name = "XBOX 360 PAD - CSE 522S";

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
  printk("xbox-debug:Device disconnected!");
  struct usb_xpad *xpad = usb_get_intfdata(intf);
  if (xpad->input_created) {
    xpad->input_created = false;
    input_unregister_device(xpad->dev);
  }
  usb_free_urb(xpad->irq_in);
  usb_free_coherent(xpad->udev, XPAD_PKT_LEN, xpad->idata, xpad->idata_dma);
  kfree(xpad);
  usb_set_intfdata(intf, NULL);
}

static const struct usb_device_id xpad_table[] = {
    {.match_flags = USB_DEVICE_ID_MATCH_VENDOR | USB_DEVICE_ID_MATCH_INT_INFO,
     .idVendor = 0x045e,
     .bInterfaceClass = USB_CLASS_VENDOR_SPEC,
     .bInterfaceSubClass = 93,
     .bInterfaceProtocol = 1},
    {}};

MODULE_DEVICE_TABLE(usb, xpad_table);

static struct usb_driver xpad_driver = {
    .name = "xbox",
    .probe = xpad_probe,
    .disconnect = xpad_disconnect,
    .id_table = xpad_table,
};

module_usb_driver(xpad_driver);
MODULE_LICENSE("GPL");