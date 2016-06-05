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

pthread_mutex_t  fileMutex          = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t  peer_poolMutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t  pieces_stateMutex  = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t *file_mutex         = &fileMutex;
pthread_mutex_t *peer_pool_mutex    = &peer_poolMutex;
pthread_mutex_t *pieces_state_mutex = &pieces_stateMutex;

static int pieces_requesting = 0;
#define LOCK_FILE          pthread_mutex_lock(file_mutex);
#define LOCK_PEER          pthread_mutex_lock(peer_pool_mutex);
#define LOCK_PIECE         if (++pieces_requesting >= 10) pthread_mutex_lock(pieces_state_mutex);
#define UNLOCK_FILE        pthread_mutex_unlock(file_mutex);
#define UNLOCK_PEER        pthread_mutex_unlock(peer_pool_mutex);
#define UNLOCK_PIECE       if (pieces_requesting-- >= 10)  pthread_mutex_unlock(pieces_state_mutex);

// =================================== Following functions are some simple tools ==================================================================

/*
 * Return the index bit in bitfield 
 * bflen is the length of bitfield(bytes)
 */
int get_bit_at_index(char *bitfield, int index, int bflen) {
	if (index < 0) {
		printf("get_bit_at_index, index < 0 =%d\n", index);
		return -1;
	}

	int byte = index / 8;
	if (byte >= bflen) {
		printf("In gen_bit_at_index, byte too big %d > %d\n", byte, bflen); 
		return -1;
	}

	int i   =  7 - index % 8;
	int bit =  (bitfield[byte] & (1 << i)) == 0 ? 0 : 1;
	//printf("get_bit_at_index: index:%d, i:%d, bitfield[%d]:%d, bit:%d\n", index, i, byte, bitfield[byte], bit);
	return bit;
}

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

int Recv( int fd, void *bp, size_t len)
{
#ifdef DEBUG
    printf("in readn: read %d char\n", len);
#endif
    int cnt;
    int rc;
    cnt = len;
    while ( cnt > 0 ) {
        rc = recv( fd, bp, cnt, 0 );
        if ( rc < 0 )               /* read error? */
        {
            if ( errno == EINTR )   /* interrupted? */
                continue;           /* restart the read */
	    printf("Recv error, %s\n", strerror(errno));
            return -1;              /* return error */
        }
        if ( rc == 0 )              /* EOF? */
            return len - cnt;       /* return short count */
        bp += rc;
        cnt -= rc;
    }
    return len;
}


int Send( int fd, void *bp, size_t len)
{
#ifdef DEBUG
    printf("in sendn: read %d char\n", len);
#endif
    int cnt;
    int rc;
    cnt = len;
    while ( cnt > 0 ) {
        rc = send( fd, bp, cnt, 0 );
        if ( rc < 0 )               /* read error? */
        {
            if ( errno == EINTR )   /* interrupted? */
                continue;           /* restart the read */
            return -1;              /* return error */
        }
        if ( rc == 0 )              /* EOF? */
            return len - cnt;       /* return short count */
        bp += rc;
        cnt -= rc;
    }
    return len;
}

//==================== Following functions are to accept peers' connection or connect peers ======================================================

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

    LOCK_PEER;

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
    
    UNLOCK_PEER;

    printf("accepted this peer\n");
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

  LOCK_PEER;

  for (int i = 0; i < peers_num; i++) {
    peerdata *p = &(peerarr[i]);

    if (strncmp(p->ip, globalInfo.g_my_ip, 16) == 0) continue; // myself
    if (p->state == CONNECT) continue; // already exist

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

    p->state = CONNECT;
    pthread_t *peer_mt = (pthread_t *)malloc(sizeof(pthread_t));
    if ( pthread_create(peer_mt, NULL, &wait_second_handshake, (void *)sockfd) != 0 ) {
        printf("Error when create wait_first_handshake thread: %s\n", strerror(errno));
	free(peer_mt);
    }
  }// end for each peer

  UNLOCK_PEER;
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

//======================================== Following functions are to send handshakes or messages ================================================

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
	if (Send(connfd, (void *)(&seg), sizeof(seg)) <= 0) {
		printf("send handshake error: %s\n", strerror(errno));
		return -1;
 	}

	printf("send_handshake successfully\n");
  	return 1;
}

/*
 * Send a keep_alive to the peer
 * On success, 1 is returned
 * On error,  -1 is returned
 */
int send_keepalive(int connfd) {
	printf("send_keepalive\n");
	int len = 0;
	if (Send(connfd, (char *)&len, 4) < 0) {
		return -1;
	}
	return 1;
}

/*
 * Send a choke to the peer
 * On success, 1 is returned
 * On error,  -1 is returned
 */
int send_choke(int connfd, peer_t *p) {
	printf("send_choke\n");
	char buffer[5];
	int  *len = (int *)buffer;
	
	*len      = 0x1000; // reversed the order
	buffer[4] = CHOKE;  // id

	if (Send(connfd, buffer, 5) < 0) {
		printf("send_choke error\n");
		return -1;
	}

	p->peer_choking = 1;
	return 1;
}

/*
 * Send a unchoke to the peer
 * On success, 1 is returned
 * On error,  -1 is returned
 */
int send_unchoke(int connfd, peer_t *p) {
	printf("send_Unchoke\n");
	char buffer[5];
	int  *len = (int *)buffer;
	
	*len      = reverse_byte_orderi(1); // reversed the order
	buffer[4] = UNCHOKE;

	if (Send(connfd, buffer, 5) < 0) {
		printf("send_unchoke error\n");
		return -1;
	}

	p->peer_choking = 0;
	return 1;

}

/* 
 * Send an interested to the peer
 * On success, 1 is returned
 * On error,  -1 is returned
 */
int send_interested(int connfd, peer_t *p) {
	printf("send_interested\n");
	char buffer[5];
	int  *len = (int *)buffer;
	
	*len      = reverse_byte_orderi(1); // reversed the order
	buffer[4] = INTERESTED;

	if (Send(connfd, buffer, 5) < 0) {
		printf("send_interested error\n");
		return -1;
	}

	p->peer_interested = 1;
	return 1;
}

/*
 * Send an not_interested to the peer
 * On success, 1 is returned
 * On error,  -1 is returned
 */
int send_notinterested(int connfd, peer_t *p) {
	printf("send_notinterested\n");
	char buffer[5];
	int  *len = (int *)buffer;
	
	*len      = reverse_byte_orderi(1); // reversed the order
	buffer[4] = NOT_INTERESTED; // id

	if (Send(connfd, buffer, 5) < 0) {
		printf("send_notinterested error\n");
		return -1;
	}

	p->peer_interested = 0;
	return 1;
}

/*
 * Send a have to all peers
 * On success, 1 is returned
 * On error,  -1 is returned
 */
int send_have(int piece_index) {
	printf("send_have\n");
	char buffer[9];
	int *len = (int *)buffer;
	int *ind = (int *)(&(buffer[5]));

	*len = reverse_byte_orderi(5);
	*ind = reverse_byte_orderi(piece_index); // piece index field
	buffer[4] = HAVE; // id
	
	peerpool_node_t *p = g_peerpool_head;
	while (p != NULL) {
		if (p->peer->peer_pieces_state[piece_index] != 1) {
			int connfd = p->peer->connfd;
			if (Send(connfd, buffer, 9) < 0) {
				printf("send_have error.\n");
			}
			printf("send a have to : %s\n", p->peer->peer_ip);
		}
		p = p->next;
	}

	return 1;
}

/*
 * Send a bitfield to the peer
 * On success, 1 is returned
 * On error,   -1 is returned
 */
int send_bitfield(int connfd) {
	LOCK_FILE;

	printf("send_bitfield:\n");
	int x_len = globalInfo.g_torrentmeta->num_pieces/8 + 1; // bitfield's len
	char buffer[5];
	int *len = (int *)buffer;
	*len = reverse_byte_orderi(x_len + 1);
	buffer[4] = BITFIELD;

	if (Send(connfd, buffer, 5) < 0) {
		printf("send_bitfield send buffer error\n");
		UNLOCK_FILE;
		return -1;
	}

	printf("send_bitfield x_len:%d\n", x_len);
	globalInfo.bitfield = gen_bitfield(globalInfo.g_torrentmeta->pieces, 
			globalInfo.g_torrentmeta->piece_len,
			globalInfo.g_torrentmeta->num_pieces);

	printf("bitfield: ");
	for(int i = 0; i < x_len; i ++) 
		printf("(send_bitfield) %x ", globalInfo.bitfield[i]);
	printf("\n");
	if (Send(connfd, globalInfo.bitfield, x_len) < 0) {
		printf("send_bitfield send bitfield error\n");
		UNLOCK_FILE;
		return -1;
	}
	
	UNLOCK_FILE;
	return 1;
}


/*
 * Send a request to the peer
 * length cannot be bigger than 2^17(bytes)
 * On success, 1 is returned
 * On error,  -1 is returned
 */
int send_request(int connfd, int index, int begin, int length) {
	char buffer[17];
	int *len = (int *)buffer;
	int *index_ = (int *)(&(buffer[5]));
	int *begin_ = (int *)(&(buffer[9]));
	int *length_ = (int *)(&(buffer[13]));
	*len = reverse_byte_orderi(13);
	*index_ = reverse_byte_orderi(index);
	*begin_ = reverse_byte_orderi(begin);
	*length_ = reverse_byte_orderi(length);
	buffer[4] = REQUEST;

	if (Send(connfd, buffer, 17) < 0) {
		printf("send_request error\n");
		return -1;
	}

	return 1;
}

/*
 * Send a piece to the peer
 * On success, 1 is returned
 * On error,  -1 is returned
 */
int send_piece(int connfd, int index, int begin, int block_len) {
	char *buffer = (char *)malloc(block_len + 13);
	memset(buffer,0,block_len + 13);
	//char buffer[block_len + 13];
	char *block  = &(buffer[13]);
	int *len = (int *)buffer;
	int *index_ = (int *)(&(buffer[5]));
	int *begin_ = (int *)(&(buffer[9]));
	*len = reverse_byte_orderi(9 + block_len);
	*index_ = reverse_byte_orderi(index);
	*begin_ = reverse_byte_orderi(begin);
	buffer[4] = PIECE;

	if (get_block(index, begin, block_len, block) < 0) {
		printf("send_piece, index:%d, begin:%d, block_len:%d failed\n",index, begin, block_len);
		return -1;
	}

	/*int j = 0;
	for(j = 0 ; j < block_len + 13;j++)
	{
		printf("%d,", buffer[j]);
	}
	printf("\n");*/
	if (Send(connfd, buffer, block_len + 13) < 0) {
		printf("send_piece error\n");
		free(buffer);
		return -1;
	}

	printf("send_piece: index %d, begin %d, block_len %d successfully\n", index, begin, block_len);
        free(buffer);
	return 1;
}

/*
 * Send a cancle to the peer
 * On success, 1 is returned
 * On error,  -1 is returned
 */
int send_cancle(int connfd, int index, int begin, int length) {
	char buffer[17];
	int *len = (int *)buffer;
	int *index_ = (int *)(&(buffer[5]));
	int *begin_ = (int *)(&(buffer[9]));
	int *length_ = (int *)(&(buffer[13]));
	*len = reverse_byte_orderi(13);
	*index_ = reverse_byte_orderi(index);
	*begin_ = reverse_byte_orderi(begin);
	*length_ = reverse_byte_orderi(length);
	buffer[4] = CANCEL;

	if (Send(connfd, buffer, 17) < 0) {
		printf("send_request error\n");
		return -1;
	}

	return 1;

}

//========================================= Following functions are designed for requesting pieces ================================================

/*
 * Seperate a piece request to 4 sub-piece requests
 */
void request_a_piece(int connfd, peer_t *p, int index, int length) {
	printf("requesting 4 sub-pieces of piece%d\n", index);
	int sub_pieces_num = 4;
	int sub_piece_len  = length / sub_pieces_num; //
	int sub_begin = 0;

	globalInfo.pieces_state_arr[index] = PIECE_REQUESTING;
	send_interested(connfd, p);
	for (int i = 0; i < sub_pieces_num; i ++) {
		if (index * length + sub_begin + sub_piece_len > globalInfo.g_torrentmeta->flist.size)
		{
		sub_piece_len = globalInfo.g_torrentmeta->flist.size - index * length - sub_begin;
		}	
		send_request(connfd, index, sub_begin, sub_piece_len);
		sub_begin += sub_piece_len;
	}
}

/*
 * Chose a piece to be requested
 * Using Least-First strategy
 * On success, the index of the piece is returned
 * If there is no piece can be requested, -1 is returned
 */
int which_piece_to_request() {
	int *my_piece_state = globalInfo.pieces_state_arr;
	int *arr = globalInfo.pieces_num_in_peers;
	int min;
	int min_val;
	for (min = 0; min < globalInfo.g_torrentmeta->num_pieces; min ++) {
		if (my_piece_state[min] == PIECE_HAVNT && arr[min] > 0) break;
	}
	if (min == globalInfo.g_torrentmeta->num_pieces) {
		printf("no piece can be requested\n");
		return -1;
	}

	min_val = arr[min];
	for (int i = min; i < globalInfo.g_torrentmeta->num_pieces; i ++) {
		if (arr[i] < min_val)
			min = i;
	}
	printf("which_piece_to_request: piece index %d\n", min);
	return min;
}

/*
 * Chose a peer to request the piece
 */
peer_t *find_peer_have_piece(int index) {
	peerpool_node_t *p = g_peerpool_head;
	peer_t *chosen; 

	while (p != NULL) {
		if (get_peer_piece_state(p->peer, index) == 1) {
			chosen = p->peer;
			break;
		}
		p = p->next;
	}

	while (p != NULL) {
		if (get_peer_piece_state(p->peer, index) == 1 &&
		    p->peer->pieces_num_downloaded_from_it < chosen->pieces_num_downloaded_from_it)
			chosen = p->peer;
		p = p->next;
	}

	return chosen;
}

/*
 * A thread
 * To request all the pieces
 */
void *request_file(void *arg) {
	while (globalArgs.isseed == 0) { 
		int index = which_piece_to_request();
		if (index >= 0) {
			peer_t *p = find_peer_have_piece(index);
			if (p != NULL) {
				LOCK_PIECE; // will be unlocked in handle_piece when received the whole piece
				request_a_piece(p->connfd, p, index, globalInfo.g_torrentmeta->piece_len);
			}
		}
		else 
			sleep(1); 
	}
}

void create_request_file_thread() {
	static int created;  
	if (created != 0) {
		printf("request_file_thread has been created\n");
		return; // ensure this thread to be created only once
	}

	pthread_t *pt = (pthread_t *)malloc(sizeof(pthread_t));
	if (pthread_create(pt, NULL, request_file, NULL) < 0) 
		printf("create_request_file_thread failed, %s\n", strerror(errno));
	else {
		printf("create_request_file_thread successfully\n");
		created = 1;
	}
}

//============================= Following functions are to handle the received handshakes or messages =============================================

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
    int *seghash = (int *)seg->sha1_hash;
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

    LOCK_PEER; 
   
    if (find_peernode(peer_ip) != NULL)  {
		printf("connection already exist\n");
		UNLOCK_PEER;
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
 	if (send_handshake(connfd) <= 0) {
		 printf("handle_handshake send_handshake error\n");
		 UNLOCK_PEER;
 		 return -1;
	}
     }

     // build connection
     peer_t *peerT = pool_add_peer(connfd, peer_ip);
     if (peerT == NULL) {
	     UNLOCK_PEER;
	     return -1;
     }

     // create a thread to handle the following messages
     message_handler_arg *arg = (message_handler_arg *)malloc(sizeof(message_handler_arg));
     arg->connfd   = connfd;
     arg->peerT    = peerT;
     arg->peerData = find_peer_from_tracker(peer_ip);
     pthread_t *peer_mt = (pthread_t *)malloc(sizeof(pthread_t));
     if ( pthread_create(peer_mt, NULL, message_handler, (void *)arg) != 0 ) {
        printf("Error when create message_handler thread: %s\n", strerror(errno));
	UNLOCK_PEER;
        return -1;
     }

     UNLOCK_PEER;
     printf("build connection with the peer %s successfully\n", peer_ip);
     return 1;
}

static inline void handle_keepalive(int connfd) {
	// do nothing
	printf("handle_keepalive\n");
	send_keepalive(connfd);
}


static inline void handle_choke(peer_t *p) {
	printf("handle_choke \n");
	p->am_choking = 1;
}

static inline void handle_unchoke(peer_t *p) {
	printf("handle_unchoke \n");
	p->am_choking = 0;
}

static inline void handle_interested(peer_t *p) {
	printf("handle_interested \n");
	p->am_interested = 1;
	if (p->peer_choking == 1)
		send_unchoke(p->connfd, p);
}

static inline void handle_notinterested(peer_t *p) {
	printf("handle_nointerested \n");
	p->am_interested = 0;
}

static inline void handle_have(int connfd, peer_t *p, int index) {
	LOCK_FILE;

	printf("handle_have: \n");
	// send request
	if (globalInfo.pieces_state_arr[index] == PIECE_HAVNT) {
		printf ("doesn't have this piece\n");
		// send unchoke and interest
		if (p->peer_choking == 1)       send_unchoke(connfd, p);
		if (p->peer_interested == 0)	send_interested(connfd, p); 

		// request_a_piece(connfd, p, index, globalInfo.g_torrentmeta->piece_len);
		send_interested(connfd, p);
		// globalInfo.pieces_state_arr[index] = PIECE_REQUESTING;
	}
	printf("handle_have successfully\n");

	UNLOCK_FILE;
}

static inline void handle_bitfield(int connfd, peer_t *p, char *bitfield, int bitfield_len) { 
	printf("handle_bitfield:\n");
	int pieces_num = globalInfo.g_torrentmeta->num_pieces;
	if (bitfield_len < (pieces_num/8 + 1)) {
			printf("handle_bitfield, bitfield_len %d < %d, error\n", bitfield_len, pieces_num/8 + 1); 
			return;
	}
	//printf("handle_bitfield, bitfield_len:%d, bitfield:", bitfield_len);
	//for (int i = 0; i < bitfield_len; i ++) printf("%x ", (unsigned char)(bitfield[i]));
	//printf("\n");

	LOCK_FILE;
	for (int i = 0; i < pieces_num; i++) {
		printf("piece%d state:%d peer_bit:%d\n",i, globalInfo.pieces_state_arr[i], get_bit_at_index(bitfield, i, bitfield_len));

		int piece_state = globalInfo.pieces_state_arr[i];
		int peer_bit = get_bit_at_index(bitfield, i, bitfield_len);

		if (peer_bit == 1) {
			peer_have_piece(p, i);
		}
		else continue;

		if (piece_state == PIECE_HAVNT) {
			// send interset and unchoke
			if (p -> peer_choking    == 1)
				send_unchoke(connfd, p);
			if (p -> peer_interested == 0)
				send_interested(connfd, p);
			// request_a_piece(connfd, p, i, globalInfo.g_torrentmeta->piece_len);
		        // globalInfo.pieces_state_arr[i] = PIECE_REQUESTING;
		}
	}
	printf("handle_bitfield successfully\n");
}

static inline void handle_request(int connfd, peer_t *p, int index, int begin, int length) {	
	LOCK_FILE;

	printf("handle_request, index:%d, begin:%d, length:%d \n", index, begin, length);
	if (p->peer_choking == 0)
		send_piece(connfd, index, begin, length);
	else 
		printf("This peer is choking %s\n", p->peer_ip);
	
        UNLOCK_FILE;
}

static inline void handle_piece(int connfd, peer_t *p, int index, int begin, int block_len, char *block) {
	LOCK_FILE;
	printf("handle_piece, index:%d, begin:%d, block_len:%d \n", index, begin, block_len);
	set_block(index, begin, block_len, block);
	int bitfield_len = globalInfo.g_torrentmeta->num_pieces/8 + 1;

	globalInfo.bitfield = gen_bitfield(globalInfo.g_torrentmeta->pieces, 
			globalInfo.g_torrentmeta->piece_len, 
			globalInfo.g_torrentmeta->num_pieces);

	// check whether this piece completed or not
	int *piece_state = &(globalInfo.pieces_state_arr[index]);
	if (get_bit_at_index(globalInfo.bitfield, index, bitfield_len) == 1) {
		// the whole piece is compelted
		*piece_state = PIECE_COMPLETED;
		p->pieces_num_downloaded_from_it ++;
		send_have(index);
		UNLOCK_PIECE;
	}
	else {
		UNLOCK_FILE;
		return;
	}

	// check whether the whole file completed or not 
	for (int i = 0; i < globalInfo.g_torrentmeta->num_pieces; i ++) {
		printf("check file if completed ");
		if (get_bit_at_index(globalInfo.bitfield, i, bitfield_len) != 1) { 
			UNLOCK_FILE;
			return; // file transform is not completed
		}
	}
	// file transform is completed, close the connfd
	printf("This file is completed\n");
	globalArgs.isseed = 1;
	close(connfd);
	
	UNLOCK_FILE;
}

static inline void handle_cancel(int connfd, int index, int begin, int length) {
	// sorry??
}

/*
 * handle the messages following handshake
 */
void *message_handler(void *arg) {
  printf("enter message_handler\n");

  message_handler_arg *peerInfo = (message_handler_arg *)arg;
  int connfd = peerInfo->connfd;
  peer_t *peerT = peerInfo->peerT;
  send_bitfield(connfd);
  int count = 0;
  int len;
  while (Recv(connfd, &len, sizeof(int)) > 0) {
	  printf("received len: %x, reversed: %x connfd:%d\n", len, reverse_byte_orderi(len),connfd);
	  len = reverse_byte_orderi(len);
	  if (len > 0) {
		  char id;
		  if (Recv(connfd, &id, 1) <= 0) {
			  printf("Recv id error\n");
			  goto error_disconnect;
		  }
		  printf("received id: %x\n", id);
		  switch(id) {
			  case CHOKE: handle_choke(peerT); break;
		          case UNCHOKE: handle_unchoke(peerT); break;
			  case INTERESTED: handle_interested(peerT); break;
			  case NOT_INTERESTED: handle_notinterested(peerT); break;
		          case HAVE: {
					     int index;
					     if (Recv(connfd, &index, 4) <= 0) { 
						     printf("RECV HAVE index error\n");
						     goto error_disconnect;
					     }
					     index = reverse_byte_orderi(index);
					     handle_have(connfd, peerT, index);
					     
					     break;
				     }
		          case BITFIELD: {
						 int x_len = len - 1;
						 char *bitfield = (char *)malloc(x_len);
						 if (Recv(connfd, bitfield, x_len) <= 0) {
							 printf("Recv BITFIELD bitfield error\n");
							 goto error_disconnect;
						 }
						 handle_bitfield(connfd, peerT, bitfield, x_len);
						 break;
					 }
			  case REQUEST: {
						int index, begin, length;
						if (Recv(connfd, &index, 4) <= 0) {
							printf("Recv REQUEST index error\n");
							goto error_disconnect;
						}
						if (Recv(connfd, &begin, 4) <= 0) {
							printf("Recv REQUEST begin error\n");
							goto error_disconnect;
						}
						if (Recv(connfd, &length, 4) <= 0) {
							printf("Recv REQUEST length error\n");
							goto error_disconnect;
						}
						index = reverse_byte_orderi(index);
						begin = reverse_byte_orderi(begin);
						length= reverse_byte_orderi(length);
						handle_request(connfd, peerT, index, begin, length);
						break;
					}
			  case PIECE: {
					      int block_len = len - 9;
					      int index, begin;
					      char *block = (char *)malloc(block_len);
					      if (Recv(connfd, &index,  4) <= 0) {
						      printf("Recv PIECE index error\n");
						      goto error_disconnect;
					      }
					      if (Recv(connfd, &begin, 4) <= 0) {
						      printf("Recv PIECE begin error\n");
						      goto error_disconnect;
					      }
					      if (Recv(connfd, block, block_len) <= 0) {
						      printf("Recv PIECE block error\n");
						      goto error_disconnect;
					      }
					      index = reverse_byte_orderi(index);
					      begin = reverse_byte_orderi(begin);
					      handle_piece(connfd, peerT, index, begin, block_len, block);
					      break;
				      }
		           case CANCEL: {
						int index, begin, length;
						if (Recv(connfd, &index, 4) <= 0) {
							printf("Recv CANCEL index error\n");
							goto error_disconnect;
						}
						if (Recv(connfd, &begin, 4) <= 0) {
							printf("Recv CANCEL begin error\n");
							goto error_disconnect;
						}
						if (Recv(connfd, &length, 4) <= 0) {
							printf("Recv CANCEL length error\n");
							goto error_disconnect;
						}
						index = reverse_byte_orderi(index);
						begin = reverse_byte_orderi(begin);
						length= reverse_byte_orderi(length);
						handle_cancel(connfd, index, begin, length);
						break;
					}
			   default : {
					     printf("unkown id: %x\n", id);
					     break;
				     }
		  } // switch
	  } // len > 0
	  else 
	  {  count++;
	  	 printf("count:%d\n", count);
		  handle_keepalive(connfd);
		
		}

  } // end while

  // deal with disconnect
error_disconnect:
  {  
	  pthread_mutex_lock(peer_pool_mutex);
	  printf("a peer disconnect %s due to %s\n", peerInfo->peerT->peer_ip, strerror(errno));
	  peerpool_remove_node(peerInfo->peerT);
	  if (peerInfo->peerData != NULL) {
		  printf("peer exist: turn its state to DISCONNECT\n");
		  peerInfo->peerData->state = DISCONNECT;
	  }
	  pthread_mutex_unlock(peer_pool_mutex);
  }

  free(peerInfo);
  return NULL;
}

