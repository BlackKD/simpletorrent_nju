#include "peer_wire.h"
#include "util.h"
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
 * Check whether we can accept the connection request or not
 * If we can accept and the peer exist, a pointer to the peer is returned
 * Else, NULL is returned
 */
peerdata *can_accept(int connfd) {
    // check if the ip exist in peer list in tracker and
    // check if the state of the peer is disconnect
    char peer_ip[INET_ADDRSTRLEN];
    if (get_ip_by_socket(connfd, peer_ip) == 0) {
	    printf("get_ip_by_socket in can_accept error\n");
	    return NULL; 
    }

    peerdata *p = find_peer_from_tracker(peer_ip);
    if (p != NULL && p->state == DISCONNECT)
        return p;
    else {
	    printf("peer %x, peer state: %d", p, (p==NULL)?-1:p->state);
        return NULL;
    }
}

/*
 * Accepting a peer:
 * create a message_handler thread to wait the handshake from the peer
 * On error, -1 will be returned
 * On success, 1 will be returned
 */
int peer_accept(int connfd) {
    printf("enter peer_accept, ");
    // check if the ip exist in peer list in tracker and
    // check if the state of the peer is disconnect
    char peer_ip[INET_ADDRSTRLEN];
    if (get_ip_by_socket(connfd, peer_ip) == 0) {
	    printf("get_ip_by_socket in peer_accept error\n");
	    return -1; 
    }

    peerdata *p = find_peer_from_tracker(peer_ip);

    // assume that all the peers connect us is valid
    if (! (p == NULL || (p != NULL && p->state == DISCONNECT))) {
	    printf("peer %x, peer state: %d ", p, (p==NULL)?-1:p->state);
            return -1;
    }
    if (p != NULL) p->state = CONNECT;

    pthread_t *peer_mt = (pthread_t *)malloc(sizeof(pthread_t));
    if ( pthread_create(peer_mt, NULL, &wait_first_handshake, (void *)connfd) != 0 ) {
        printf("Error when create wait_first_handshake thread: %s\n", strerror(errno));
	free(peer_mt);
        return -1;
    }	
    
    printf("accepted this peer\n");
    return 1;
}

/*
 * Wait a handshake from the peer, which is the first handshake
 * If received, call handshake_handler(flag = 1) to deal with the handshke
 */
void *wait_first_handshake(void *arg) {
 	printf("in wait_first_handshake\n");
 	int connfd = (int)arg;
 	handshake_seg seg;

 	if (recv(connfd, &seg, sizeof(seg), 0) <= 0) {
 		printf("connfd: %d, recv error\n", connfd);
 		return NULL;
 	}

 	// received
 	if (seg.c != 19 || strncmp(seg.str, "BitTorrent protocol", 20) != 0) {
 		printf("invalid handshake\n");
 		close(connfd);
 		return NULL;
 	}

 	// valid handshake, handle it
 	if (handshake_handler(&seg, 1, connfd) <= 0) {
 		printf("error handshake, close this.\n");
 		close(connfd);
 	}

	printf("leave wait_first_handshake, recv first handshake and handled it\n");
 	return NULL;
}


/*
 * Wait a handshake from the peer, which is the second handshake(I sent the first)
 * If received, call handshake_handler(flag = 2)to deal with the handshke
 */
void *wait_second_handshake(void *arg) {
 	printf("in wait_second_handshake\n");
 	int connfd = (int)arg;
 	handshake_seg seg;

 	if (recv(connfd, &seg, sizeof(seg), 0) <= 0) {
 		printf("recv second_handshake error\n");
 		return NULL;
 	}

 	// received
 	if (seg.c != 19 || strncmp(seg.str, "BitTorrent protocol", 20) != 0) {
 		printf("second_handshake, invalid handshake\n");
		handshake_print(&seg);
		printf("seg.c: %d, seg.str: %s\n", seg.c, seg.str); 
 		close(connfd);
 		return NULL;
 	}

 	// valid handshake, handle it
 	// if it's the one who send try to connect the other later, close this connection
 	if (handshake_handler(&seg, 2, connfd) <= 0) {
 		printf("close this.\n");
		set_peer_disconnect(connfd);
 		close(connfd);
 		return NULL;
 	}

	printf("leave wait_second_handshake, receive handshake and handled it\n");
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
	handshake_print(seg);

     // check hash
    int *myhash  = globalInfo.g_torrentmeta->info_hash;
    int *seghash = seg->sha1_hash;
    for (int i = 0; i < 5; i ++) {
      if (seghash[i] != reverse_byte_orderi(myhash[i])) {
        printf("handshake_handler, Invalid hash\n");
        return -1;
      }
    }

    char peer_ip[INET_ADDRSTRLEN];
    if (get_ip_by_socket(connfd, peer_ip) == 0) {
	    printf("get_ip_by_socket in handshake_handler error.\n");
	    return -1;
    }

  	if (find_peernode(peer_ip) != NULL)  {
		printf("connection already exist\n");
		return 0;// already exist the connection
	}

    // check whether the peer id is contained in peer lists in tracker responses
    // But now, we assume that it's one of the peers in the peer list
  	/*peerdata *p = find_peer_from_tracker(peer_ip);
  	if (p == NULL) {
  		printf("Not the right peer, not exists in the tracker response\n");
  		return -1; // not the right peer
  	}*/

  	if (flag == 1) {
 		   if (send_handshake(connfd) <= 0)
 			   return -1;
  	}

  	// build connection
  	peer_t *peerT = pool_add_peer(connfd, peer_ip);
  	if (peerT == NULL) return -1;

  	// create a thread to handle the following messages
  	message_handler_arg *arg = (message_handler_arg *)malloc(sizeof(message_handler_arg));
	arg->connfd   = connfd;
  	arg->peerT    = peerT;
  	arg->peerData = find_peer_from_tracker(peer_ip);
  	pthread_t *peer_mt = (pthread_t *)malloc(sizeof(pthread_t));
	  if ( pthread_create(peer_mt, NULL, message_handler, (void *)arg) != 0 ) {
        printf("Error when create message_handler thread: %s\n", strerror(errno));
        return -1;
	  }

	printf("build connection with the peer %s successfully\n", peer_ip);
	return 1;
}

/*
 * Send bitfield and then
 * handle the messages following handshake
 */
void *message_handler(void *arg) {
  printf("enter message_handler\n");
  message_handler_arg *peerInfo = (message_handler_arg *)arg;
  int connfd = peerInfo->connfd;
  char buffer;
  while (recv(connfd, &buffer, 1, 0) > 0) {
	  //printf("recv %1x from %s\n", buffer, peerInfo->peerT->peer_ip);
  }

  // deal with disconnect
  {  
	  printf("a peer disconnect %s due to %s\n", peerInfo->peerT->peer_ip, strerror(errno));
	  peerpool_remove_node(peerInfo->peerT);
	  if (peerInfo->peerData != NULL) {
		  printf("turn its state to DISCONNECT\n");
		  peerInfo->peerData->state = DISCONNECT;
	  }
  }

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
  	printf("In send_handshake\n");

  	handshake_seg seg;
	memset(&seg, 0, sizeof(seg));
  	seg.c = 19;
  	strncpy(seg.str, "BitTorrent protocol", 20);

  	int *myhash  = globalInfo.g_torrentmeta->info_hash;
	int *seghash = (int *)seg.sha1_hash;
  	char *myid   = globalInfo.g_my_id;
	for (int i = 0; i < 5; i ++) {
		seghash[i] = reverse_byte_orderi(myhash[i]);
	}
	for (int i = 0; i < 20; i ++) {
		seg.peer_id[i]   = myid[i];
 	}

    // send it
	handshake_print(&seg);
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
	printf("enter peer_connect\n");
	//if (globalArgs.isseed == 1) return;

  tracker_data *data      = globalInfo.g_tracker_response;
  peerdata     *peerarr   = data->peers;
  int           peers_num = data->numpeers;

  for (int i = 0; i < peers_num; i++) {
    peerdata *p = &(peerarr[i]);

    if (strncmp(p->ip, globalInfo.g_my_ip, 16) == 0) continue; // myself
    if (p->state == CONNECT) continue; // already exist

    p->state = CONNECT;
    printf("connecting peer: %s, %d\n", p->ip, p->port);
    // socket
    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
      printf("i: %d peer socket error, %s\n", i, strerror(errno));
      continue;
    }
    // connect
    struct sockaddr_in peer_addr;
    peer_addr.sin_family      = AF_INET;
    peer_addr.sin_port        = htons(p->port);
    peer_addr.sin_addr.s_addr = inet_addr(p->ip);
    if (connect(sockfd, (struct sockaddr*)(&peer_addr), sizeof(peer_addr)) < 0) {
      printf("i: %d peer connect error, %s\n", i, strerror(errno));
      continue;
    }
    // connect successfully, now send the first handshake and create wait the second handshake thread
    if (send_handshake(sockfd) < 0) {
      close(sockfd);
      continue;
    }
    pthread_t *peer_mt = (pthread_t *)malloc(sizeof(pthread_t));
    if ( pthread_create(peer_mt, NULL, &wait_second_handshake, (void *)sockfd) != 0 ) {
        printf("Error when create wait_first_handshake thread: %s\n", strerror(errno));
	free(peer_mt);
    }
  }// end for each peer
}
