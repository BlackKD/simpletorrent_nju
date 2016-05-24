#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <arpa/inet.h>
#include "fileio.h"
#include "sha1.h"
#include "mytorrent.h"



// open or create file and set file's size
FILE *createfile(char *filepath, int size){
    if (!exists(filepath)){
        printf("createfile %s\n", filepath);
        int fd = creat(filepath, 0777);
        if (fd == -1){
            int tmp = errno;
            printf("%s\n", strerror(tmp));
            return NULL;
        }
        close(fd);
    }
    FILE *fp = fopen(filepath, "r+");
    if (fp == NULL){
        int tmp = errno;
        printf("%s\n", strerror(tmp));
        return NULL;
    }
    int cursize = filesize(fp);
    if (cursize < size){
        fseek(fp, size - 1, SEEK_SET);
        fputc('a', fp);
    }
    fseek(fp, 0, SEEK_SET);
    return fp;
}

// return size of file
int filesize(FILE *fp){
    int cur = ftell(fp);
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    fseek(fp, cur, SEEK_SET);
    return size;
}

// check whether file exist
bool exists(char *filepath){
    if (access(filepath, F_OK) != -1)
        return true;
    return false;
}


//获取从begin开始的分片len大小的长度 放入buf
int list_get_piece(struct fileinfo_t *fileinfo, char *buf, int len, int begin){
    if (fileinfo->begin_index + fileinfo->size < begin + len){
        printf("write beyond file list\n");
        return -1;
    }
        if (fileinfo->begin_index <= begin && fileinfo->begin_index + fileinfo->size > begin){
            // write to a file
            if (fileinfo->begin_index + fileinfo->size - begin - len >= 0){
                fseek(fileinfo->fp, begin - fileinfo->begin_index, SEEK_SET);
                return fread(buf, sizeof(char), len, fileinfo->fp);
            }
            else
            	{
            		printf("too big in list_get_piece\n");
            		assert(0);
            	}
        }
    
    return -1;
}


//将buf写入begin开始的分片len大小的长度
int list_set_piece(struct fileinfo_t *fileinfo,char *buf, int len, int begin){
    if (fileinfo->begin_index + fileinfo->size < begin + len){
        printf("write beyond file list\n");
        return -1;
    }
        if (fileinfo->begin_index <= begin && fileinfo->begin_index + fileinfo->size > begin){
            // write to a file
            if (fileinfo->begin_index + fileinfo->size - begin - len >= 0){
                fseek(fileinfo->fp, begin - fileinfo->begin_index, SEEK_SET);
                return fwrite(buf, sizeof(char), len, fileinfo->fp);
            } 
            else
            	{
            		printf("too big in list_get_piece\n");
            		assert(0);
            	}
        }
   
    return -1;
}

static char set_bit[8] = {1,2,4,8,16,32,64,128};

void set_bit_at_index(char *info, int index, int bit){
    int offset = 7 - index%8;
    if(bit)
        info[index/8] = info[index/8] | set_bit[offset];
    else
        info[index/8] = info[index/8] & (~set_bit[offset]);
}

// generate bitfield //COUNT LOCAL SHA CMPARE TO GET FROM TORRENT AND SET BITFIELD
char *gen_bitfield(char *piece_hash, int piece_len, int piece_num){
    int cursize = globalInfo.g_torrentmeta->flist.begin_index + globalInfo.g_torrentmeta->flist.size;
    char *bitfield = (char *)malloc(piece_num / 8 + 1);
    memset(bitfield, 0, piece_num / 8 + 1);
    char *hashbuf = (char *)malloc(piece_len);
    struct hashptr_t * ptr = (struct hashptr_t *) piece_hash;
    int i;
    for (i = 0; i < piece_num; i++){
        int blocksize = (i != piece_num - 1)?piece_len:(cursize - (piece_num - 1) * piece_len);
        list_get_piece(&globalInfo.g_torrentmeta->flist, hashbuf, blocksize, piece_len * i);
        SHA1Context sha;
        SHA1Reset(&sha);
        SHA1Input(&sha, (const unsigned char*)hashbuf, blocksize);
        if(!SHA1Result(&sha))
        {
            printf("FAILURE in count sha when generate bitfield\n");
            assert(false);
        }
        int j;
        for (j = 0; j < 5; j++){
            sha.Message_Digest[j] = htonl(sha.Message_Digest[j]);
        }
#ifdef DEBUG
        for (j = 0; j < 5; j++){
            printf("%08X ", sha.Message_Digest[j]);
        }
        printf("\nvs\n");
        for (j = 0; j < 5; j++){
            printf("%08X ", ptr->hash[j]);
        }
        printf("\n\n");
#endif
        if (memcmp(sha.Message_Digest, ptr->hash, 20) == 0){
            printf("write 1\n");
            globalInfo.g_downloaded += globalInfo.g_torrentmeta->piece_len;
            set_bit_at_index(bitfield, i, 1);
        } else {
            printf("write 0\n");
            set_bit_at_index(bitfield, i, 0);
        }
        ptr++;
    }

    free(hashbuf);
    return bitfield;
}


// read special piece from file
// return actual read bytes
int get_piece(FILE *fp, char *buf, int piece_num, int piece_size){
    int cursize = filesize(fp);
    if (cursize <= piece_num * piece_size){
        printf("request unexist piece\n");
        return -1;
    }
    if (cursize < (piece_num + 1) * piece_size){
        fseek(fp, piece_num * piece_size, SEEK_SET);
        return fread(buf, sizeof(char), cursize - (piece_num * piece_size), fp);
    }
    fseek(fp, piece_num * piece_size, SEEK_SET);
    return fread(buf, sizeof(char), piece_size, fp);
}

// write special piece to file
// return actual write bytes
int set_piece(FILE *fp, char *buf, int piece_num, int piece_size){
    int cursize = filesize(fp);
    if (cursize <= piece_num * piece_size){
        printf("write unexist piece\n");
        return -1;
    }
    if (cursize < (piece_num + 1) * piece_size){
        fseek(fp, piece_num * piece_size, SEEK_SET);
        return fwrite(buf, sizeof(char), cursize - (piece_num * piece_size), fp);
    }
    fseek(fp, piece_num * piece_size, SEEK_SET);
    return fwrite(buf, sizeof(char), piece_size, fp);
}

// read sub piece from file
int get_any_piece(FILE *fp, char *buf, int begin, int len, int piece_num, int piece_size){
    int cursize = filesize(fp);
    if (cursize < piece_num * piece_size + len){
        printf("read any piece beyond file size\n");
        return -1;
    }
    fseek(fp, piece_num * piece_size + begin, SEEK_SET);
    return fread(buf, sizeof(char), len, fp);
}


void get_block(int index, int begin, int length, char *block){
    list_get_piece(&globalInfo.g_torrentmeta->flist,  block, length, index * globalInfo.g_torrentmeta->piece_len + begin);
}
void set_block(int index, int begin, int length, char *block){
    list_set_piece(&globalInfo.g_torrentmeta->flist,  block, length, index * globalInfo.g_torrentmeta->piece_len + begin);
}