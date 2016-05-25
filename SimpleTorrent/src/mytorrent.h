 #include "btdata.h"
#include <stdbool.h>

#ifndef __GLOBAL_H_
#define __GLOBAL_H_
#define DEBUG


struct globalInfo_t{
    char    g_my_ip[16];
    char    g_tracker_ip[16];
    char    g_my_id[20];
    int     g_peer_port;
    int     g_tracker_port;
    int     g_done;
    int     g_uploaded;
    int     g_downloaded;
    int     g_left;
    char    *bitfield;
    torrentmetadata_t   *g_torrentmeta;
    tracker_data        *g_tracker_response;
};


struct globalArgs_t{
    char*   torrentpath;
    int     isseed;
    int     port;
};

extern struct globalArgs_t globalArgs;
extern struct globalInfo_t globalInfo;
extern int listenfd;
#endif