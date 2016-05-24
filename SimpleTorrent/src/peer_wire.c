#include "peer_wire.h"

/*
 * Accepting a peer:
 * create a message_handler thread to wait the handshake from the peer
 * On erroe, -1 will be returned
 * On success, 1 will be returned
 */
int peer_accept(int connfd) {
	pthread_t peer_mt;
	if ( pthread_create(&peer_mt, NULL, wait_handshake, (void *)connfd) != 0 ) {
        printf("Error when create daemon thread: %s\n", strerror(errno));
        return -1;
	}
	return 1;
}

/*
 * Wait a handshake from the peer
 * If received, call handshake_handler to deal with the handshke 
 */
 void *wait_handshake(void *arg) {
 	printf("in wait_handshake, ");
 	int connfd = (int)arg;
 	handshake_seg seg;
 	
 	if (recv(connfd, &seg, sizeof(seg), NULL) <= 0) {
 		printf("\n");
 		return;
 	} 
 	
 	// received
 	if (seg.c != 19 || strncmp(seg.str, "BitTorrent protocool") != 0) {
 		printf("invalid handshake\n");
 		close(connfd);
 		return;
 	}

 	// valid handshake, handle it
 	// if it's the one who send try to connect the other later, close this connection
 	if (handshake_handler(&seg) <= 0) {
 		printf("the connection already exits, close this.\n");
 		close(connfd);
 	}
 }

 /*
  * Deal with a handshake:
  * On the case that I sent the handshake first, 0 is returned
  * On the right case, 1 is returned
  * On error, -1 is returned
  */
  int handshake_handler(handshake_seg * seg) {
  	
  }