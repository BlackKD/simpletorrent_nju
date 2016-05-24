#ifndef __PEERS_POOL_H__
#define __PEERS_POOL_H__

#define STATE_I_SEND_HANDSHAKE_FIRST
#define STATE_PEER_SEND_HANDSHAKE_FIRST
#define STATE_CONNECTED

// 针对到一个peer的已建立连接, 维护相关数据
typedef struct _peer_t {
    int connfd;
    int state;
    int choking;        // 作为上传者, 阻塞远端peer
    int interested;     // 远端peer对我们的分片有兴趣
    int choked;         // 作为下载者, 我们被远端peer阻塞
    int have_interest;  // 作为下载者, 对远端peer的分片有兴趣
    char peer_id[20]; 
} peer_t;


#endif