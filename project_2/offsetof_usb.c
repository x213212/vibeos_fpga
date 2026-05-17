#include <stdio.h>
#include <stddef.h>

typedef unsigned int uint32;
typedef unsigned char uint8;
typedef unsigned short uint16;

struct ehci_qtd {
    uint32 next;
    uint32 alt_next;
    uint32 token;
    uint32 buf[5];
    uint32 buf_hi[5];
} __attribute__((packed, aligned(32)));

struct ehci_qh {
    uint32 horiz;
    uint32 ep_char;
    uint32 ep_cap;
    uint32 cur_qtd;
    struct ehci_qtd overlay;
} __attribute__((packed, aligned(64)));

struct usb_setup_pkt {
    uint8 bmRequestType;
    uint8 bRequest;
    uint16 wValue;
    uint16 wIndex;
    uint16 wLength;
} __attribute__((packed));

struct usb_mouse_state {
    struct ehci_qh async_head;
    struct ehci_qh ctrl_qh;
    struct ehci_qh intr_qh;
    struct ehci_qtd qtd[4];
    struct usb_setup_pkt setup;
    uint8 data[256] __attribute__((aligned(32)));
    uint8 report[8] __attribute__((aligned(32)));
    int ready;
    int addr;
    int intr_ep;
    int intr_mps;
    int ep0_mps;
    int speed;
    int speed_try;
    int last_fail;
    int poll_div;
    int retry_div;
    uint32 last_buttons;
} __attribute__((aligned(4096)));

int main(void)
{
    printf("sizeof qtd=%zu qh=%zu state=%zu\n",
           sizeof(struct ehci_qtd), sizeof(struct ehci_qh), sizeof(struct usb_mouse_state));
    printf("async=%zu ctrl=%zu intr=%zu qtd=%zu setup=%zu data=%zu report=%zu ready=%zu\n",
           offsetof(struct usb_mouse_state, async_head),
           offsetof(struct usb_mouse_state, ctrl_qh),
           offsetof(struct usb_mouse_state, intr_qh),
           offsetof(struct usb_mouse_state, qtd),
           offsetof(struct usb_mouse_state, setup),
           offsetof(struct usb_mouse_state, data),
           offsetof(struct usb_mouse_state, report),
           offsetof(struct usb_mouse_state, ready));
    return 0;
}
