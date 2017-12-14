#include <canbus.h>

#include <fcntl.h>
#include <errno.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/can.h>
#include <linux/can/raw.h>

int can_open(CanBusT* canbus,  const char *can_name) {
        int nbytes;
        struct sockaddr_can addr;
        struct can_frame frame;
        struct ifreq ifr;
        
        // const char *ifname = "vcan0";
        const char *ifname = can_name;
        
        if ((canbus->socket = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0)
        {
            perror("Error while opening socket");
            return -1;
        }
        
        strcpy(ifr.ifr_name, ifname);
        ioctl(canbus->socket, SIOCGIFINDEX, &ifr);
        
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        
        printf("%s at index %d\n", ifname, ifr.ifr_ifindex);
        
        if (bind(canbus->socket, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            perror("Error in socket bind");
            return -2;
        }
        
        int ret = fcntl(canbus->socket, F_SETOWN, getpid());
        
        if (ret < 0)
        {
            perror("Setting owner process of socket failed");          
            return -2;
        }
        
        if (ret == 0)
        {
            ret = fcntl(canbus->socket, F_SETFL, FASYNC | O_NONBLOCK);
            if (ret < 0)
            {
                perror("Setting nonblocking asynchronous socket I/O failed");
                return -2;
            }
        }
        
        if (ret < 0)
        {
            return errno;
        }
        
        return 0;

}


CanFrame can_read(CanBusT* canbus) {
        struct can_frame rx_frame;
        memset(&rx_frame, 0, sizeof(struct can_frame));
        
        int nbytes = read(canbus->socket, &rx_frame, CAN_MTU);
        
        //printf("Read %d bytes\n", nbytes); 
        
        CanFrame result;
        
        if (nbytes < 0)
        {
            result.err_no = errno;
            return result;
        }
        
        result.err_no = 0;
        result.id = rx_frame.can_id;
        result.length = rx_frame.can_dlc;
        result.data = (unsigned char*) malloc(rx_frame.can_dlc);
        memcpy(result.data, rx_frame.data, rx_frame.can_dlc);
        return result;

}

int can_write(CanBusT* canbus, int id, void* data, int length) {

        struct can_frame tx_frame;
        memset(&tx_frame, 0, sizeof(tx_frame));
        
        tx_frame.can_id = id;
        tx_frame.can_dlc = length;
        
        memcpy(tx_frame.data, data, length);
        
        int nbytes = write(canbus->socket, &tx_frame, sizeof(tx_frame));
        
        // printf("Wrote %d bytes\n", nbytes);
        
        if (nbytes == -1)
        {
            perror("Couldn't write to socket");
            return errno;
        }
        return nbytes;

}

int can_close(CanBusT* canbus) {
        int ret = close(canbus->socket);
        if (ret < 0)
        {
            perror("Failed to close can socket");
            return errno;
        }
        return 0;

}

