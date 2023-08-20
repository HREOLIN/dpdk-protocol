#include "ping.h"

struct icmp
{
	unsigned char type;
	unsigned char code;
	unsigned short chksum;

	unsigned short id;
	unsigned short seq;

	unsigned long tv_sec;
	unsigned long tv_usec;

	unsigned char unused[DATASIZE]; 
};
/**********************
 * 函数名：
 * 功能：
 * 参数：
 * 返回值：
 * **********************/
unsigned short Ping_calchecksum(char *buffer, int len)
{
	unsigned short *p = (unsigned short *)buffer;
	unsigned int sum = 0;
	int i = 0;
	
	for (i = 0; i < len/2; i++) 
	{
		sum += *p;
		p++;
	}
	sum += (sum >> 16);
	return ~sum; 
}

/**********************
 * 函数名：
 * 功能：
 * 参数：
 * 返回值：
 * **********************/
int Ping_pack(int pack_no)
{
	bzero(&g_icmp_data, sizeof(g_icmp_data));
	g_icmp_data.type = PA_RESQUST;
	g_icmp_data.code = 0;
	g_icmp_data.chksum = 0;
	g_icmp_data.id = 0;
	g_icmp_data.seq = 0;
	struct timeval tv;
	gettimeofday(&tv, NULL); 

	g_icmp_data.tv_sec = tv.tv_sec;
	g_icmp_data.tv_usec = tv.tv_usec;
	memset(g_icmp_data.unused, 0, DATASIZE);

	g_icmp_data.chksum = Ping_calchecksum((char *)&g_icmp_data, sizeof(g_icmp_data));
	
	return 0;
}


/*********************
 * 函数名：Ping_mysend
 * 功能：发送ICMP请求报文
 * 参数：num 发送请求包的次数
 * 返回值：0
 * **********************/
int Ping_mysend(int num)
{
	int packetsize = 0;
	int i = 0;
	char ch = 0;
	int rlen = 0;

	for (i = 0; i < num; i++)
	{
		packetsize = Ping_pack(i);
		if (sendto(g_sockfd, &g_icmp_data, sizeof(g_icmp_data), 0, (struct sockaddr *)&g_ser, sizeof(g_ser)) < 0)
		{
			perror("sendto");
			continue;
		}
		else
		{
			g_begin++;
			sleep(1);
		}
		
	}
	return 0;
}



void* send_data(void* arg) 
{
	int num = 0;
	
	if (0 == g_array[0])
	{
		Ping_mysend(PING_A);	
	}
	else if (0 == strncmp(g_array, "-a", 2))
	{
		Ping_mysend(PING_A);
			
	}
	else if (0 == strncmp(g_array, "-t", 2))
	{
		Ping_mysend(PING_T);
	}
	else if (0 == strncmp(g_array, "-n", 2))
	{
		num = Ping_gettimeofn();	
		Ping_mysend(num);
	}

	return 	NULL;
}



/**********************
 * 函数名：Ping_gettimeofn
 * 功能：获得ping -n conut 参数conut的值
 * 返回值： sum
 * **********************/
int Ping_gettimeofn()
{
	int sum = 0;
	
	sum = atoi(g_nconut);
	
	return sum;
}


/**********************
 * 函数名：Ping_myrecv
 * 功能：接收icmp回复报文
 * 参数：num
 * 返回值：接收的次数，依据参数和发送的次数定
 * **********************/
int Ping_myrecv(int num)
{
	int rlen = 0;
	int i = 0;
	char ch = 0;
	int ret = 0;
	
	while (i < num)
	{
		memset(g_buffer_recv, 0, sizeof(g_buffer_recv));

		rlen = recv(g_sockfd, g_buffer_recv, sizeof(g_buffer_recv), 0);
		if (rlen < 0)
		{
			printf("fail to recv!\n");
			continue;
		}
		i++;	
		ret = Ping_unpack(rlen);
		
		if (-1 == ret)
		{
			continue;
		}
		else
		{
			g_end++;
		}	
	}
	return 0;
}

void* recv_data(void *arg)
{
	int num = 0;
	
	if (0 == g_array[0])
	{
		Ping_myrecv(PING_A);	
	}
	else if (0 == strncmp(g_array, "-a", 2))
	{
		Ping_myrecv(PING_A);
			
	}
	else if (0 == strncmp(g_array, "-t", 2))
	{
		Ping_myrecv(PING_T);
	}
	else if (0 == strncmp(g_array, "-n", 2))
	{
		num = Ping_gettimeofn();
			
		Ping_myrecv(num);
	}
	
	return NULL;
}

/**********************
 * 函数名：Ping_unpack
 * 功能：解析响应数据包、判断是否返回正常
 * 参数：len 响应数据包长度
 * 返回值：成功返回0，失败返回-1
 * **********************/
int Ping_unpack(int len)
{
	double rtt = 0;
	struct timeval tv_cur;
	struct ip *myip;
	struct icmp *myicmp;
	int ipsize = 0;	
	
	char *recv_ip = NULL;
	
	myip = (struct ip*)g_buffer_recv;
	ipsize = (myip->ip_hl)<<2;
	
	recv_ip = (char *)inet_ntoa(myip->ip_src);
	len -= ipsize;

	myicmp = (struct icmp *)(g_buffer_recv + ipsize);
	if (len < UP_ICMPHEAD)   
	{
		perror("ICMP packets length is less than 8\n");
		return -1;
	}
	else if(strcmp(g_ip, recv_ip)) 
	{
		return -1;
	}	
	
	else if (myicmp->type != 0) 
	{
		return -1;
	}	
	else
	{
		gettimeofday(&tv_cur, NULL);
		rtt = (tv_cur.tv_sec - g_icmp_data.tv_sec)*1000 + (tv_cur.tv_usec - g_icmp_data.tv_usec)/1000.0f;
	
		g_totaltime += rtt;
		if (rtt > g_maxtime)
		{
			g_maxtime = rtt;
		}	  
		if (rtt < g_mintime)
		{
			g_mintime = rtt;
		}

		printf("%d byte from %s: icmp_seq=%u ttl=%d rtt=%0.1fms\n", g_datalen + UP_ICMPHEAD, inet_ntoa(g_ser.sin_addr), g_end,  myip->ip_ttl, rtt);
	}

	return 0;
}

/**********************
 * 函数名：sig_int
 * 功能：收到结束线程信号后，打印数据
 * 参数：signo
 * **********************/
void sig_int(int signo)
{
	double perce = 0;

	perce = (g_begin - g_end) / g_begin;
	printf("---ping %s is complete--- \n %d was sended and %d was received, %0.1lf%%packet loss, time %0.1fms rtt\n min/avg/max = %0.1f/%0.1f/%0.1f ms \n", g_tmpbuff, g_begin, g_end, perce * 100 , g_totaltime, g_mintime, g_totaltime/g_begin, g_maxtime);

	exit(0);
}

int main(int argc, char *argv[])
{
	double number = 0;
	struct hostent *ent = NULL;
	pthread_t pid1 = 0, pid2 = 0;

	
	signal(SIGINT, sig_int);
	
	snprintf(g_tmpbuff, sizeof(g_tmpbuff), "%s", argv[1]);

	if (argc < 2)
	{
		return -1;
	}
	if (argv[1] == NULL)
	{
		perror("fail to ping!");
		return -1;
	}
	if (argc > 2)
	{
		memset(g_array, 0, sizeof(g_array));
		memset(g_nconut, 0, sizeof(g_nconut));
		snprintf(g_array, sizeof(g_array), "%s", argv[2]);
	}
	if (argc > 3)
	{	
	 	if (0 == strncmp(g_array, "-n", 2))
		{
			snprintf(g_nconut, sizeof(g_nconut), "%s", argv[3]); 
		}
	}	
		
	ent = gethostbyname(argv[1]);
				
	if (NULL == ent)
	{
		printf("ping: unknown host %s\n", argv[1]);
		exit(-1);
	}
			
	inet_ntop(AF_INET, ent->h_addr, g_ip, sizeof(g_ip));  
	
			
			
	g_sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
			
	
	bzero(&g_ser, sizeof(g_ser));
	g_ser.sin_family = AF_INET;
	inet_pton(AF_INET, g_ip, &g_ser.sin_addr); 
	

	printf("PING %s (%s) %d bytes of data.\n", g_tmpbuff, g_ip, g_datalen);
	
	if(pthread_create(&pid1, NULL, send_data, NULL) < 0)
	{
		 perror("pthread_create");
		 return -1;
	}
	 if(pthread_create(&pid2, NULL, recv_data, NULL) < 0)
	{
		 perror("pthread_create");
		  return -1;
	 }

	pthread_join(pid1, NULL);
	pthread_join(pid2, NULL);
				 
	number = (g_begin - g_end) * 100 / g_begin;
	printf("---ping %s is complete--- \n %d was sended and %d was received, %0.1lf%%packet loss, time %0.1fms \n min/avg/max = %0.1f/%0.1f/%0.1f ms \n", argv[1], g_begin, g_end, number, g_totaltime, g_mintime, g_totaltime/g_begin, g_maxtime);

	close(g_sockfd);
	
	return 0;
}



