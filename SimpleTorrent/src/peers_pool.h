#ifndef __PEERS_POOL_H__
#define __PEERS_POOL_H__

#define TRUE 1  // enable  a state
#define FALSE 0  // disable a state

#include "mytorrent.h"
#include "btdata.h"

typedef struct _peerpool_node_t {
	peer_t *peer;
	struct _peerpool_node_t *next;
} peerpool_node_t;

extern peerpool_node_t *g_peerpool_head;

peerpool_node_t *find_peernode(char peer_ip[16]);
peerdata *find_peer_from_tracker(char peer_ip[16]);
peer_t *pool_add_peer(int connfd, char peer_ip[16]);
peerdata *set_peer_disconnect(int connfd);
void peerpool_remove_node(peer_t *peerT);

#endif
