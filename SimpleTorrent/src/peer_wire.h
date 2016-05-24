#ifndef __PEERWIRE_H__
#define __PEERWIRE_H__


int peer_accept(int connfd);
void *wait_handshake(void *);

struct handshake_seg_ {
	char c;             // should be '\19'
	char str[20];       // should be "BitTorrent protocol"
	char reserved[8];   // eight reserved bytes
	char sha1_hash[20]; // 20-byte sha1 hash, the same value which is announced as info_hash to the tracker
	char peer_id[20];   // 20-byte peer id which is reported in tracker requests and contained in peer lists in tracker responses
};
typedef struct handshake_seg_ handshake_seg;

#endif