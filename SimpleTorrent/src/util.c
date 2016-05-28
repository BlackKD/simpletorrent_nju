
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>

#include "util.h"

int get_addr_by_socket(int sockfd, struct sockaddr_in *addr) {
    int len = sizeof(struct sockaddr_in);
    memset(addr, 0, sizeof(struct sockaddr_in));


    if (getpeername(sockfd, (struct sockaddr*)addr, &len) != 0) {
        printf("getppername failed\n");
        return -1;
    }

    return 1;
}

/* On error, 0 is returned */
unsigned long get_ip_by_socket(int sockfd, char *ip_buffer) {
	struct sockaddr_in peer_addr;
	if (get_addr_by_socket(sockfd, &peer_addr) < 0) {
		printf("get_addr_by_socket error. %s\n", strerror(errno));
		return 0;
	}
	if (!inet_ntop(AF_INET, (void *)(&(peer_addr.sin_addr)), ip_buffer, INET_ADDRSTRLEN)) {
		printf("inet_ntop in get_ip_by_socket error. %s\n", strerror(errno));
		return 0;
	}
	printf("peer_ip in socket: %s\n", ip_buffer);
	return peer_addr.sin_addr.s_addr;
}

int connect_to_host(char* ip, int port)
{
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    perror("Could not create socket");
    return(-1);
  }

  struct sockaddr_in addr;
  memset(&addr,0,sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(ip);
  addr.sin_port = htons(port);

  if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
  {
    perror("Error connecting to socket");
    return(-1);
  }

  return sockfd;
}

int make_listen_port(int port)
{
  int sockfd;

  sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if(sockfd <0)
  {
    perror("Could not create socket");
    return 0;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if(bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
  {
      perror("Could not bind socket");
      return 0;
  }

  if(listen(sockfd, 20) < 0)
  {
    perror("Error listening on socket");
    return 0;
  }

  return sockfd;
}

// ����һ�����ļ����ֽ���
int file_len(FILE* fp)
{
  int sz;
  fseek(fp , 0 , SEEK_END);
  sz = ftell(fp);
  rewind (fp);
  return sz;
}

// recvline(int fd, char **line)
// ����: ���׽���fd�����ַ���
// ����: �׽���������fd, �����յ�������д��line
// ���: ��ȡ���ֽ���
int recvline(int fd, char **line)
{
  int retVal;
  int lineIndex = 0;
  int lineSize  = 128;

  *line = (char *)malloc(sizeof(char) * lineSize);

  if (*line == NULL)
  {
    perror("malloc");
    return -1;
  }

  while ((retVal = read(fd, *line + lineIndex, 1)) == 1)
  {
    if ('\n' == (*line)[lineIndex])
    {
      (*line)[lineIndex] = 0;
      break;
    }

    lineIndex += 1;

    /*
      �����õ��ַ�̫��, �����·����л���.
    */
    if (lineIndex > lineSize)
    {
      lineSize *= 2;
      char *newLine = realloc(*line, sizeof(char) * lineSize);

      if (newLine == NULL)
      {
        retVal    = -1; /* reallocʧ�� */
        break;
      }

      *line = newLine;
    }
  }

  if (retVal < 0)
  {
    free(*line);
    return -1;
  }
  #ifdef NDEBUG
  else
  {
    fprintf(stdout, "%03d> %s\n", fd, *line);
  }
  #endif

  return lineIndex;
}
/* End recvline */

// recvlinef(int fd, char *format, ...)
// ����: ���׽���fd�����ַ���.�������������ָ��һ����ʽ�ַ���, ��������洢��ָ���ı�����
// ����: �׽���������fd, ��ʽ�ַ���format, ָ�����ڴ洢������ݵı�����ָ��
// ���: ��ȡ���ֽ���
int recvlinef(int fd, char *format, ...)
{
  va_list argv;
  va_start(argv, format);

  int retVal = -1;
  char *line;
  int lineSize = recvline(fd, &line);

  if (lineSize > 0)
  {
    retVal = vsscanf(line, format, argv);
    free(line);
  }

  va_end(argv);

  return retVal;
}
/* End recvlinef */

int reverse_byte_orderi(int i)
{
  unsigned char c1, c2, c3, c4;
  c1 = i & 0xFF;
  c2 = (i >> 8) & 0xFF;
  c3 = (i >> 16) & 0xFF;
  c4 = (i >> 24) & 0xFF;
  return ((int)c1 << 24) + ((int)c2 << 16) + ((int)c3 << 8) + c4;
}
