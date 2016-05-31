#ifndef __PEERWIRE_H__
#define __PEERWIRE_H__

#include "btdata.h"
#include "mytorrent.h"
#include "peers_pool.h"

enum message_id {
	CHOKE         = 0,
	UNCHOKE       = 1,
	INTERESTED    = 2,
	NOT_INTERESTED= 3,
	HAVE          = 4,
	BITFIELD      = 5,
	REQUEST       = 6,
	PIECE         = 7,
	CANCEL        = 8
};

#pragma pack(1)
struct handshake_seg_ {
	char c;             // should be '\19'
	char str[20];       // should be "BitTorrent protocol"
	char reserved[7];   // eight reserved bytes
	char sha1_hash[20]; // 20-byte sha1 hash, the same value which is announced as info_hash to the tracker
	char peer_id[20];   // 20-byte peer id which is reported in tracker requests and contained in peer lists in tracker responses
};
typedef struct handshake_seg_ handshake_seg;
#pragma

static void handshake_print(handshake_seg *s) {
	unsigned char *p = (unsigned char *)s;
	int size = sizeof(handshake_seg);
	printf("handshake size: %d, content: ", s->c, size);
	for (int i = 0; i < size; i ++) {
		printf("%1x ", p[i]); 
	}
	printf("\n");
}

struct message_handler_arg_ {
	int connfd;
	peer_t *peerT;
	peerdata *peerData;
};
typedef struct message_handler_arg_ message_handler_arg;

int peer_accept(int connfd);
void peer_connect();
void *wait_first_handshake(void *arg);
void *wait_second_handshake(void *arg);
int handshake_handler(handshake_seg * seg, int flag, int connfd);
void *message_handler(void *arg);
int send_handshake(int connfd);

#endif
