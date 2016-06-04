#include <stdlib.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <getopt.h>
#include "fileio.h"
#include <stdbool.h>
#include <errno.h>
#include "util.h"
#include "btdata.h"
#include "bencode.h"
#include "peer_wire.h"

extern unsigned long get_local_ip(char *);
extern int *init_pieces_state_arr();
extern int *init_pieces_num_in_peers();

struct globalArgs_t globalArgs;
int listenfd;
struct globalInfo_t globalInfo;

void useage()
{
    printf("Usage: ./SimpleTorrent [isseed] <torrentpath> n");
    printf("\t isseed is optional, 1 indicates this is a seed and won't contact other clients\n");
    printf("\t 0 indicates a downloading client and will connect to other clients and receive connections\n");
    exit(-1);
}

//链接来自其它peer的链接
void *port_listen(void *arg){
    int sockfd = (int)arg;

    printf("daemon is running\n");
    for(;;){
        struct sockaddr_in cliaddr;
        socklen_t cliaddr_len;
        cliaddr_len = sizeof(struct sockaddr_in);
        int connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &cliaddr_len);
        if (connfd < 0){
            int tmp = errno;
            printf("Error when accept socket:%s\n", strerror(tmp));
            continue;
        }
        printf("receive a connect from %s\n", inet_ntoa(cliaddr.sin_addr));
        //！！！！添加p2p处理程序  套接字为connfd
        peer_accept(connfd);
    }

    return NULL;
}

int main ( int argc, char *argv[] )
{
	if(argc<=2)
		useage();

    globalInfo.pieces_state_arr = NULL;
    globalInfo.pieces_num_in_peers = NULL;
    globalArgs.port = 8530;
    globalArgs.isseed = 0;
    globalArgs.torrentpath = NULL;
    globalArgs.isseed = atoi(argv[1]);
    globalArgs.torrentpath = argv[2];
    get_local_ip(globalInfo.g_my_ip);

#ifdef DEBUG
    printf("isseed:%d\n", globalArgs.isseed);
    printf("port:%d\n", globalArgs.port);
    printf("torrentpath:%s\n", globalArgs.torrentpath);
#endif

	//init info
    int i = 0;
   	int val[5];
  	for(i=0; i<5; i++)
  	{
    val[i] = rand();
  	}
    memcpy(globalInfo.g_my_id,(char*)val,20);
    globalInfo.g_peer_port = globalArgs.port;
    globalInfo.g_torrentmeta = parsetorrentfile(globalArgs.torrentpath); 
    if (globalInfo.g_torrentmeta == NULL){
        printf("Error when parsing torrent file\n");
        return -1;
    }
    //creatfile from torrent
     globalInfo.g_torrentmeta->flist.fp = createfile(globalInfo.g_torrentmeta->flist.filename, globalInfo.g_torrentmeta->flist.size);
     globalInfo.bitfield = gen_bitfield(globalInfo.g_torrentmeta->pieces, globalInfo.g_torrentmeta->piece_len, globalInfo.g_torrentmeta->num_pieces);

#ifdef DEBUG
    printf("bitfield:");
    for (i = 0; i <= globalInfo.g_torrentmeta->num_pieces / 8; i++)
        printf("%X ", globalInfo.bitfield[i]);
    printf("\n");
#endif 

    // listen to port
    listenfd = make_listen_port(globalInfo.g_peer_port);
    if (listenfd == 0){
        printf("Error when create socket for binding:%s\n", strerror(errno));
        exit(-1);
    }

    pthread_t p_listen;
    if (pthread_create(&p_listen, NULL, port_listen, (void *)listenfd) != 0){
        int tmp = errno;
        printf("Error when create daemon thread: %s\n", strerror(tmp));
        return -1;
    }

    announce_url_t *announce_info = parse_announce_url(globalInfo.g_torrentmeta->announce);
     printf("host: %s:%d\n", announce_info->hostname, announce_info->port);

     struct hostent *tracker_hostent = gethostbyname(announce_info->hostname);
       if (tracker_hostent==NULL)
  		{ 
    		printf("gethostbyname(%s) failed", announce_info->hostname);
    		exit(1);
  		}

    strcpy(globalInfo.g_tracker_ip, inet_ntoa(*((struct in_addr *)tracker_hostent->h_addr_list[0])));
    globalInfo.g_tracker_port = announce_info->port;
    printf("localhost: %s:%d\n", globalInfo.g_tracker_ip, globalInfo.g_tracker_port);
    free(announce_info);
    announce_info = NULL;


      // 初始化
  // 设置信号句柄
  signal(SIGINT,client_shutdown);

  // 设置监听peer的线程
  // 定期联系Tracker服务器
  int event = BT_STARTED;
  while(true)
  {
  	int sockfd = connect_to_host(globalInfo.g_tracker_ip, globalInfo.g_tracker_port);
  	int request_len = 0;
    char *request = make_tracker_request(event, &request_len, &globalInfo);
    event = -1;
#ifdef DEBUG
        printf("request to tracker:%s", request);
#endif
        if(send(sockfd, request, request_len, 0) == -1)
         {
            int tmp = errno;
            printf("%s\n", strerror(tmp));
            // error when connect to tracker, wait some time and try again
            sleep(5);
            close(sockfd);
            free(request);
            continue;
        }

    tracker_response* tr;
    tr = preprocess_tracker_response(sockfd); 
   
    // 关闭套接字, 以便再次使用
    shutdown(sockfd,SHUT_RDWR);
    close(sockfd);
    sockfd = 0;


    printf("Decoding response...\n");
    char* tmp2 = (char*)malloc(tr->size*sizeof(char));
    memcpy(tmp2,tr->data,tr->size*sizeof(char));

    printf("Parsing tracker data\n");
    globalInfo.g_tracker_response = get_tracker_data(tmp2,tr->size);
    printf("heer\n");
    free(tmp2);
    tmp2 = NULL;
    free(tr->data);
    free(tr);

    init_pieces_state_arr();
    init_pieces_num_in_peers();


//#ifdef DEBUG
        printf("Num Peers: %d\n", globalInfo.g_tracker_response->numpeers);
        for (i = 0; i < globalInfo.g_tracker_response->numpeers; i++){
            printf("Peer ip: %s:%d\n", globalInfo.g_tracker_response->peers[i].ip, globalInfo.g_tracker_response->peers[i].port);
        }
//#endif

    //主动去链接需要的peer
    //所需的信息的都在
    //globalInfo.g_tracker_response->peers[i].ip, globalInfo.g_tracker_response->peers[i].port
    peer_connect();

  	printf("sleep %d seconds\n", globalInfo.g_tracker_response->interval);
    sleep(globalInfo.g_tracker_response->interval);
  }
}
