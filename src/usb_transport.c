#include "usb_transport.h"
#include "td2130.h"

#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct td_usb {
    libusb_context *context;
    libusb_device_handle *handle;
    uint8_t ep_in;
    uint8_t ep_out;
    int interface_number;
    int detached;
};

static void err(char *buf, size_t size, const char *what, int code) {
    snprintf(buf, size, "%s: %s", what, libusb_error_name(code));
}

int td_usb_list(void) {
    libusb_context *context = NULL;
    libusb_device **devices = NULL;
    int rc = libusb_init(&context);
    if (rc) {
        fprintf(stderr, "libusb_init: %s\n", libusb_error_name(rc));
        return -1;
    }
    ssize_t count = libusb_get_device_list(context, &devices);
    if (count < 0) {
        fprintf(stderr, "USB enumeration: %s\n", libusb_error_name((int)count));
        libusb_exit(context);
        return -1;
    }
    int found = 0;
    for (ssize_t i = 0; i < count; ++i) {
        struct libusb_device_descriptor d;
        if (libusb_get_device_descriptor(devices[i], &d) || d.idVendor != TD2130_USB_VID) continue;
        found++;
        printf("bus=%03u device=%03u vid=%04x pid=%04x",
               libusb_get_bus_number(devices[i]), libusb_get_device_address(devices[i]),
               d.idVendor, d.idProduct);
        libusb_device_handle *h = NULL;
        rc = libusb_open(devices[i], &h);
        if (rc) {
            printf(" access=%s", libusb_error_name(rc));
        } else {
            unsigned char product[128] = {0};
            if (d.iProduct) libusb_get_string_descriptor_ascii(h, d.iProduct, product, sizeof(product) - 1);
            printf(" access=ok");
            if (product[0]) printf(" product=\"%s\"", product);
            struct libusb_config_descriptor *cfg = NULL;
            if (!libusb_get_active_config_descriptor(devices[i], &cfg)) {
                for (uint8_t n = 0; n < cfg->bNumInterfaces; ++n) {
                    const struct libusb_interface *it = &cfg->interface[n];
                    for (int a = 0; a < it->num_altsetting; ++a) {
                        const struct libusb_interface_descriptor *alt = &it->altsetting[a];
                        printf(" interface=%u class=%02x endpoints=", alt->bInterfaceNumber,
                               alt->bInterfaceClass);
                        for (uint8_t e = 0; e < alt->bNumEndpoints; ++e)
                            printf("%s%02x", e ? "," : "", alt->endpoint[e].bEndpointAddress);
                        int active = libusb_kernel_driver_active(h, alt->bInterfaceNumber);
                        if (active >= 0) printf(" kernel-driver=%s", active ? "yes" : "no");
                    }
                }
                libusb_free_config_descriptor(cfg);
            }
            libusb_close(h);
        }
        if (d.idProduct == TD2130_USB_PID) printf(" supported=yes");
        else printf(" supported=no");
        putchar('\n');
    }
    if (!found) puts("No Brother USB devices (vendor 04f9) found.");
    libusb_free_device_list(devices, 1);
    libusb_exit(context);
    return found ? 0 : 1;
}

int td_usb_open(td_usb **out, char *error, size_t error_size) {
    td_usb *u = calloc(1, sizeof(*u));
    if (!u) return -1;
    u->interface_number = -1;
    int rc = libusb_init(&u->context);
    if (rc) { err(error, error_size, "libusb_init", rc); goto fail; }
    libusb_device **devices = NULL;
    ssize_t count = libusb_get_device_list(u->context, &devices);
    if (count < 0) { err(error, error_size, "enumerate USB devices", (int)count); goto fail; }
    int open_rc = LIBUSB_ERROR_NO_DEVICE;
    for (ssize_t i = 0; i < count; ++i) {
        struct libusb_device_descriptor d;
        if (!libusb_get_device_descriptor(devices[i], &d) &&
            d.idVendor == TD2130_USB_VID && d.idProduct == TD2130_USB_PID) {
            open_rc = libusb_open(devices[i], &u->handle);
            break;
        }
    }
    libusb_free_device_list(devices, 1);
    if (!u->handle) {
        if (open_rc == LIBUSB_ERROR_ACCESS)
            snprintf(error, error_size, "TD-2130N found, but USB access was denied; install the udev rule or test with sudo");
        else if (open_rc != LIBUSB_ERROR_NO_DEVICE)
            err(error, error_size, "open TD-2130N USB device", open_rc);
        else
            snprintf(error, error_size, "TD-2130N (04f9:2058) not found");
        goto fail;
    }

    struct libusb_config_descriptor *cfg = NULL;
    rc = libusb_get_active_config_descriptor(libusb_get_device(u->handle), &cfg);
    if (rc) { err(error, error_size, "get USB configuration", rc); goto fail; }
    for (uint8_t i = 0; i < cfg->bNumInterfaces && u->interface_number < 0; ++i) {
        const struct libusb_interface *it = &cfg->interface[i];
        for (int a = 0; a < it->num_altsetting; ++a) {
            const struct libusb_interface_descriptor *alt = &it->altsetting[a];
            uint8_t in = 0, out_ep = 0;
            for (uint8_t e = 0; e < alt->bNumEndpoints; ++e) {
                const struct libusb_endpoint_descriptor *ep = &alt->endpoint[e];
                if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK) continue;
                if (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) in = ep->bEndpointAddress;
                else out_ep = ep->bEndpointAddress;
            }
            if (in && out_ep) { u->ep_in = in; u->ep_out = out_ep; u->interface_number = alt->bInterfaceNumber; break; }
        }
    }
    libusb_free_config_descriptor(cfg);
    if (u->interface_number < 0) { snprintf(error, error_size, "printer bulk endpoints not found"); goto fail; }
    if (libusb_kernel_driver_active(u->handle, u->interface_number) == 1) {
        rc = libusb_detach_kernel_driver(u->handle, u->interface_number);
        if (rc) { err(error, error_size, "detach OS printer driver", rc); goto fail; }
        u->detached = 1;
    }
    rc = libusb_claim_interface(u->handle, u->interface_number);
    if (rc) { err(error, error_size, "claim USB interface", rc); goto fail; }
    *out = u;
    return 0;
fail:
    td_usb_close(u);
    return -1;
}

int td_usb_write(td_usb *u, const uint8_t *data, size_t len, char *error, size_t error_size) {
    while (len) {
        int chunk = len > 16384 ? 16384 : (int)len, sent = 0;
        int rc = libusb_bulk_transfer(u->handle, u->ep_out, (unsigned char *)data,
                                      chunk, &sent, 5000);
        if (rc || sent <= 0) { err(error, error_size, "USB write", rc); return -1; }
        data += sent; len -= (size_t)sent;
    }
    return 0;
}

int td_usb_read_status(td_usb *u, uint8_t status[32], unsigned timeout_ms,
                       char *error, size_t error_size) {
    const uint8_t request[] = {0x1b, 0x69, 0x53};
    if (td_usb_write(u, request, sizeof(request), error, error_size)) return -1;
    int total = 0;
    /* Some Linux host-controller/printer combinations deliver a ZLP before
       the 32-byte status packet. Also accept a status split across packets. */
    for (unsigned attempt = 0; attempt < 8 && total < 32; ++attempt) {
        int got = 0;
        int rc = libusb_bulk_transfer(u->handle, u->ep_in, status + total,
                                      32 - total, &got, timeout_ms);
        if (got > 0) total += got;
        if (rc == LIBUSB_ERROR_TIMEOUT) {
            if (total == 32) break;
            break;
        }
        if (rc) {
            err(error, error_size, "USB status read", rc);
            return -1;
        }
    }
    if (total != 32) {
        snprintf(error, error_size, "short USB status response (%d of 32 bytes)", total);
        return -1;
    }
    return 0;
}

void td_usb_close(td_usb *u) {
    if (!u) return;
    if (u->handle && u->interface_number >= 0) {
        libusb_release_interface(u->handle, u->interface_number);
        if (u->detached) libusb_attach_kernel_driver(u->handle, u->interface_number);
    }
    if (u->handle) libusb_close(u->handle);
    if (u->context) libusb_exit(u->context);
    free(u);
}
