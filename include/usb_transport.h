#ifndef USB_TRANSPORT_H
#define USB_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

typedef struct td_usb td_usb;

int td_usb_list(void);
int td_usb_open(td_usb **out, char *error, size_t error_size);
int td_usb_write(td_usb *usb, const uint8_t *data, size_t len,
                 char *error, size_t error_size);
int td_usb_read_status(td_usb *usb, uint8_t status[32], unsigned timeout_ms,
                       char *error, size_t error_size);
void td_usb_close(td_usb *usb);

#endif
