#ifndef CANBUS_H
#define CANBUS_H

#include <fcntl.h>
#include <errno.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#ifdef __cplusplus
extern "C" { 
#endif
    typedef struct CanFrame
    {
        int err_no;
        uint32_t id;
        uint8_t length;
        unsigned char *data;
    } CanFrame;
    
    typedef struct CanBusT {
        int socket;
    } CanBusT;
    
    int can_open(struct CanBusT* canbus, const char *can_name);
    CanFrame can_read(struct CanBusT* canbus);
    int can_write(struct CanBusT* canbus, int id, void *data, int length);
    int can_close(struct CanBusT* canbus);
#ifdef __cplusplus
}
#endif

#endif
