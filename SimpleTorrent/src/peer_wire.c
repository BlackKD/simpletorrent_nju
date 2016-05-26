#include "peer_wire.h"
#include <stdlib.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/*
 * Accepting a peer:
 * create a message_handler thread to wait the handshake from the peer
 * On erroe, -1 will be returned
 * On success, 1 will be returned
 */
int peer_accept(int connfd) {
	pthread_t peer_mt;
	if ( pthread_create(&peer_mt, NULL, wait_first_handshake, (void *)connfd) != 0 ) {
        printf("Error when create wait_first_handshake thread: %s\n", strerror(errno));
        return -1;
	}
	return 1;
}

/*
 * Wait a handshake from the peer, which is the first handshake
 * If received, call handshake_handler(flag = 1) to deal with the handshke 
 */
void *wait_first_handshake(void *arg) {
 	printf("in wait_handshake, ");
 	int connfd = (int)arg;
 	handshake_seg seg;
 	
 	if (recv(connfd, &seg, sizeof(seg), 0) <= 0) {
 		printf("\n");
 		return NULL;
 	} 
 	
 	// received
 	if (seg.c != 19 || strncmp(seg.str, "BitTorrent protocol", 20) != 0) {
 		printf("invalid handshake\n");
 		close(connfd);
 		return NULL;
 	}
 	// check hash
 	char *myhash = (char *)(globalInfo.g_torrentmeta->info_hash);
 	for (int i = 0; i < 20; i ++) {
 		if (seg.sha1_hash[i] != myhash[i]) 
 			return NULL;
 	}

 	// valid handshake, handle it
 	// if it's the one who send try to connect the other later, close this connection
 	if (handshake_handler(&seg, 1, connfd) <= 0) {
 		printf("the connection already exits, close this.\n");
 		close(connfd);
 	}

 	return NULL;
}


/*
 * Wait a handshake from the peer, which is the second handshake(I sent the first)
 * If received, call handshake_handler(flag = 2)to deal with the handshke 
 */
void *wait_second_handshake(void *arg) {
 	printf("in wait_handshake, ");
 	int connfd = (int)arg;
 	handshake_seg seg;
 	
 	if (recv(connfd, &seg, sizeof(seg), 0) <= 0) {
 		printf("\n");
 		return NULL;
 	} 
 	
 	// received
 	if (seg.c != 19 || strncmp(seg.str, "BitTorrent protocool", 20) != 0) {
 		printf("invalid handshake\n");
 		close(connfd);
 		return NULL;
 	}

 	// valid handshake, handle it
 	// if it's the one who send try to connect the other later, close this connection
 	if (handshake_handler(&seg, 2, connfd) <= 0) {
 		printf("the connection already exits, close this.\n");
 		close(connfd);
 		return NULL;
 	}

 	return NULL;
} 

/*
 * Deal with a handshake:
 * Args: 
 * flag 1 for first handshake, 2 for second handshake
 * Return:
 * On the case that the connection should be closed, 0 is returned
 * On the right case, 1 is returned and a new thread to receive messages will be created
 * On error, -1 is returned
 */
int handshake_handler(handshake_seg * seg, int flag, int connfd) {
  	printf("handshake_handler %d\n", flag);
  	 
     // check hash
    char *myhash = (char *)(globalInfo.g_torrentmeta->info_hash);
    for (int i = 0; i < 20; i ++) {
      if (seg->sha1_hash[i] != myhash[i]) {
        printf("Invalid hash\n");
        return NULL;
      }
    }

    char *peer_id = seg->peer_id;

  	if (find_peernode(peer_id) != NULL) // already exist the connection
  		return 0;
  	
    // check whether the peer id is contained in peer lists in tracker responses 
  	peerdata *p = find_peer_from_tracker(seg->peer_id);
  	if (p == NULL) {
  		printf("Not the right peer, not exists in the tracker response\n");
  		return -1; // not the right peer
  	}

  	if (flag == 1) {
 		   if (send_handshake(connfd) <= 0)
 			   return -1;
  	}
  		
  	// build connection
  	peer_t *peerT = pool_add_peer(connfd, peer_id); 
  	if (peerT == NULL) return -1;

  	// create a thread to handle the following messages 
  	message_handler_arg *arg = (message_handler_arg *)malloc(sizeof(message_handler_arg));
  	arg->peerT    = peerT;
  	arg->peerData = p; 
  	pthread_t peer_mt;
	  if ( pthread_create(&peer_mt, NULL, message_handler, (void *)arg) != 0 ) {
        printf("Error when create message_handler thread: %s\n", strerror(errno));
        return -1;
	  }

	printf("build connection with the peer %s successfully\n", peer_id);
	return 1;
}

/*
 * Send bitfield and then
 * handle the messages following handshake
 */
void *message_handler(void *arg) {
  printf("enter message_handler ");
 	message_handler_arg *peerInfo = (message_handler_arg *)arg;

 	// todo:

 	free(peerInfo);
 	return NULL;
}

/*
 * Send a handshake to the peer
 * On success,  1 is returned
 * On error,   -1 is returned 
 */
int send_handshake(int connfd) {
  	printf("In send_handshake, ");

  	handshake_seg seg;
  	seg.c = 19;
  	strncpy(seg.str, "BitTorrent protocol", 20);

  	char *myhash = (char *)(globalInfo.g_torrentmeta->info_hash);
  	char *myid   = globalInfo.g_my_id;
 	  for (int i = 0; i < 20; i ++) {
 		 seg.sha1_hash[i] = myhash[i];
 		 seg.peer_id[i]   = myid[i];
 	  }

    // send it
 	  if (send(connfd, (void *)(&seg), sizeof(seg), 0) <= 0) {
 		 printf("send handshake error: %s\n", strerror(errno));
 		 return -1;
 	  }

 	  printf("send_handshake successfully\n");
  	return 1;
}

/*
 * Connect peers actively
 */
void peer_connect() {
  tracker_data *data      = globalInfo.g_tracker_response;
  peerdata     *peerarr  = data->peers;
  int           peers_num = data->numpeers;

  for (int i = 0; i < peers_num; i++) {
    peerdata p = peerarr[i];
    if (find_peernode(p.id) != NULL) continue; // already exist
    
    printf("connecting peer: %s, %d\n", p.id, p.port);
    // socket
    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
      printf("i: %d peer socket error, %s\n", i, strerror(errno));
      continue;
    }  
    // connect
    struct sockaddr_in peer_addr;
    peer_addr.sin_family      = AF_INET;
    peer_addr.sin_port        = htons(p.port);
    peer_addr.sin_addr.s_addr = inet_addr(p.id);
    if (connect(sockfd, (struct sockaddr*)(&peer_addr), sizeof(peer_addr)) < 0) {
      printf("i: %d peer connect error, %s\n", i, strerror(errno));
      continue;
    }
    // connect successfully, now send the first handshake and create wait the second handshake thread
    if (send_handshake(sockfd) < 0) {
      close(sockfd);
      continue;
    }
    pthread_t peer_mt;
    if ( pthread_create(&peer_mt, NULL, wait_second_handshake, (void *)sockfd) != 0 ) {
        printf("Error when create wait_first_handshake thread: %s\n", strerror(errno));
    }
  }// end for each peer
}