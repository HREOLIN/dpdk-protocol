#ifndef PING__H_
#define PING__H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#include <signal.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <pthread.h>

#define BUFFER_SIZE       512
#define MAX_SIZE          56
#define ARRAY_SIZE        12
#define NCONUT_SIZE       12
#define ARGV1             128
#define MINSIZE           1000
#define DATASIZE          48
#define PING_A            4
#define PING_T            100
#define UP_ICMPHEAD       8
#define PA_RESQUST        8

struct icmp g_icmp_data;
struct sockaddr_in g_ser;
int g_begin = 0;
int g_end = 0;
int g_sockfd = 0;
char g_buffer_recv[BUFFER_SIZE] = {0};
int g_datalen = MAX_SIZE;
char g_array[ARRAY_SIZE] = {0};
char g_nconut[NCONUT_SIZE] = {0};
char g_tmpbuff[ARGV1] = {0};
double g_totaltime = 0;
double g_maxtime = 0;
double g_mintime = MINSIZE;
char g_ip[32] = {0};




int Ping_compareInput();
int Ping_gettimeofn();
unsigned short Ping_calchecksum(char *buffer, int len);
int Ping_pack(int pack_no);
int Ping_mysend();
int Ping_myrecv();
int Ping_unpack(int len);

#endif


