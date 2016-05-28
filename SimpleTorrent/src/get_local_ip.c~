/*
 * Find local ip used as source ip in ip packets.
 * Read the /proc/net/route file
 */
 
#include <stdio.h> //printf
#include <string.h>    //memset
#include <errno.h> //errno
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <stdlib.h>
#include <unistd.h>
 
 /*
  * On error, 0 is returned
  * On success, the value of the ip address is returned(big endian)
  */
unsigned long get_local_ip(char *ip_buffer)
{
    FILE *f;
    char line[100] , *p , *c;
     
    f = fopen("/proc/net/route" , "r");
     
    while(fgets(line , 100 , f))
    {
        p = strtok(line , " \t");
        c = strtok(NULL , " \t");
         
        if(p!=NULL && c!=NULL)
        {
            if(strcmp(c , "00000000") == 0)
            {
                //printf("Default interface is : %s \n" , p);
                break;
            }
        }
    }
     
    //which family do we require , AF_INET or AF_INET6
    int fm = AF_INET;
    struct ifaddrs *ifaddr, *ifa;
    int family , s;
    char host[NI_MAXHOST];
    unsigned long ip_value =0;
 
    if (getifaddrs(&ifaddr) == -1) 
    {
        perror("getifaddrs");
		return 0;
    }
 
    //Walk through linked list, maintaining head pointer so we can free list later
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) 
    {
        if (ifa->ifa_addr == NULL)
        {
            continue;
        }
 
        family = ifa->ifa_addr->sa_family;
 
        if(strcmp( ifa->ifa_name , p) == 0)
        {
            if (family == fm) 
            {
                s = getnameinfo( ifa->ifa_addr, (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6) , 
                    host , NI_MAXHOST , NULL , 0 , NI_NUMERICHOST);
                 
                if (s != 0) 
                {
                    printf("getnameinfo() failed: %s\n", gai_strerror(s));
					return 0;
                }
                
		strncpy(ip_buffer, host, 16);
                unsigned ip0, ip1, ip2, ip3; // must be this order, for the position in the stack
                sscanf(host, "%u\.%u\.%u\.%u", &ip3, &ip2, &ip1, &ip0);
                printf("Host's address: %u.%u.%u.%u", ip3, ip2, ip1, ip0); // 1 byte <- 4 bytes, will cover

                char *ip_val_ptr = (char *)(&ip_value);
                ip_val_ptr[3] = (char)ip0; 
                ip_val_ptr[2] = (char)ip1;
                ip_val_ptr[1] = (char)ip2;
                ip_val_ptr[0] = (char)ip3;
            }
            printf("\n");
        }
    }
 
    freeifaddrs(ifaddr);
     
    return ip_value;
}
