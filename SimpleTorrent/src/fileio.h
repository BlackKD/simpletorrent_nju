#ifndef __FILE_IO_H_
#define __FILE_IO_H_

#include <stdbool.h>
#include <stdio.h>

struct fileinfo_t {
    FILE *fp;
    char filename[80];// with path
    int begin_index;
    int size;
};

 typedef struct hashptr_t{
 	int hash[5];
 };

int filesize(FILE *fp);
bool exists(char *filepath);
FILE *createfile(char *filepath, int size);
char *gen_bitfield(char *piece_hash, int piece_len, int piece_num);
void set_bit_at_index(char *info, int index, int bit);//修改位域函数
int list_set_piece(struct fileinfo_t *fileinfo,char *buf, int len, int begin);
int list_get_piece(struct fileinfo_t *fileinfo, char *buf, int len, int begin);
//以下为主要文件读写接口函数
void get_block(int index, int begin, int length, char *block);//一对函数支持从index个分片的begin位置开始写或读length的内容，放或写入block中,支持跨分片读写
void set_block(int index, int begin, int length, char *block);
int set_piece(FILE *fp, char *buf, int piece_num, int piece_size);//按分片读写，可以多个分片一起读写，注意要提供文件指针，保存在globalInfo.g_torrentmeta->flist 中
int get_piece(FILE *fp, char *buf, int piece_num, int piece_size);
int get_any_piece(FILE *fp, char *buf, int begin, int len, int piece_num, int piece_size);
#endif