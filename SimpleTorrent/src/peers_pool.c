#include "mytorrent.h"
#include "btdata.h"
#include "peer_wire.h"
#include "peers_pool.h"
#include "util.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

peerpool_node_t *g_peerpool_head = NULL; // list

/*
 * New a peer_t,
 * init it,
 * insert it into the pool,
 * On success, return its pointer.
 * On error, return NULL
 */
peer_t *pool_add_peer(int connfd, char peer_ip[16]) {
	peer_t *p = (peer_t *)malloc(sizeof(peer_t));
	if (p == NULL) {
		printf("pool_new_peer malloc peer_t error!\n");
		return NULL;
	}
	else {
		// set peer's information
		p->connfd = connfd;
		strncpy(p->peer_ip, peer_ip, 16);
		// set states
		p->am_choking      = TRUE;
		p->am_interested   = FALSE;
		p->peer_choking    = TRUE;
		p->peer_interested = FALSE;
		p->peer_pieces_state = (int *)malloc(sizeof(int) * globalInfo.g_torrentmeta->num_pieces);
	}

	// new peerpool_node_t
	peerpool_node_t *node = (peerpool_node_t *)malloc(sizeof(peerpool_node_t));
	if (node == NULL) {
		free(p);
		printf("pool_new_peer malloc node error.\n");
		return NULL;
	}
	else {
		node->peer = p;
	}

	// insert it
	if (g_peerpool_head != NULL) {
		peerpool_node_t *temp = g_peerpool_head;
		g_peerpool_head       = node;
		g_peerpool_head->next = temp;
	}
	else {
		g_peerpool_head       = node;
		g_peerpool_head->next = NULL;
	}

	return p;
}

/*
 * Find the peer in the pool, whose peer_id == peer_id
 * On success, a pointer to the peerpool_node_t is returned
 * On failure, NULL is returned
 */
peerpool_node_t *find_peernode(char peer_ip[16]) {
	peerpool_node_t *p = g_peerpool_head;
	while (p != NULL) {
		if (strncmp(p->peer->peer_ip, peer_ip, 16) == 0) {
			return p;
		}
		else
			p = p->next;
	}
	return NULL;
}

void peerpool_remove_node(peer_t *peerT) {
	if (peerT == NULL) return;
	char *peer_ip = peerT->peer_ip;
	peerpool_node_t *p        = g_peerpool_head;
	peerpool_node_t *pre_node = NULL;
	while (p != NULL) {
		if (strncmp(peer_ip, p->peer->peer_ip, 16) == 0) {
			if (p == g_peerpool_head) 
				g_peerpool_head = p->next;
			else
				pre_node->next = p->next;
			free(p);
			break;
		}
		else {
			pre_node = p;
			p        = p->next;
		}
	}
}


/*
 * Find the peer in the tracker response, whose peer_id == peer_Id
 * On success, a pointer to the peerdata is returned
 * On failure, NULL is returned
 */
peerdata *find_peer_from_tracker(char peer_ip[16]) {
	tracker_data *data      = globalInfo.g_tracker_response;
	peerdata     *peerlist  = globalInfo.g_tracker_response->peers;
	int           peers_num = globalInfo.g_tracker_response->numpeers;

	printf("find_peer_from_tracker, peers_num:%d, ", peers_num);
	for (int i = 0; i < peers_num; i++) {
		printf("\n%d: peer_ip: %s peer.ip: %s ", i, peer_ip, peerlist[i].ip);
		if (strncmp(peer_ip, peerlist[i].ip, 16) == 0) {
			printf("find it\n");
			return &(peerlist[i]);
		}
	}

	return NULL;
}

static inline int get_peer_piece_state(peer_t *p, int index) {
	return p->peer_pieces_state[index];
}

void peer_have_piece(peer_t *p, int index) {
	p->peer_pieces_state[index] = 1;
	globalInfo.pieces_num_in_peers[index] += 1; 
}

peer_t *find_peer_have_piece(int index) {
	peerpool_node_t *p = g_peerpool_head;
	while (p != NULL) {
		if (get_peer_piece_state(p->peer, index) == 1)
			return p->peer;
	}
	return NULL;
}

peerdata *set_peer_disconnect(int connfd) {
	char peer_ip[16];
	if (get_ip_by_socket(connfd, peer_ip) == 0) {
		printf("get_ip_by_socket in set_peer_disconnect error.\n");
		return NULL;
	}
	peerdata *p = find_peer_from_tracker(peer_ip);
	if (p != NULL) {
		p->state = DISCONNECT;
		return p;
	}
	return NULL;
}
	


