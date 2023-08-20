

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_malloc.h>
#include <rte_timer.h>


#include <stdio.h>
#include <arpa/inet.h>

#include "arp.h"

#define ENABLE_SEND		1  
#define ENABLE_ARP		1
#define ENABLE_ICMP		1
#define ENABLE_ARP_REPLY	1

#define ENABLE_DEBUG		0

#define ENABLE_TIMER		1
#define ENABLE_UDP			1

#define NUM_MBUFS (4096-1)

#define BURST_SIZE	32

// 时间
#define TIMER_RESOLUTION_CYCLES 60000000000ULL // 10ms * 1000 = 10s * 3 


#if ENABLE_SEND

// ip地址转换
#define MAKE_IPV4_ADDR(a, b, c, d) (a + (b<<8) + (c<<16) + (d<<24))

// 端口ip号
static uint32_t gLocalIp = MAKE_IPV4_ADDR(192, 168, 160, 156);

// 源IP和目的IP
static uint32_t gSrcIp; //
static uint32_t gDstIp;

// 源mac和目的mac
static uint8_t gSrcMac[RTE_ETHER_ADDR_LEN];
static uint8_t gDstMac[RTE_ETHER_ADDR_LEN];

// 源端口和目的端口
static uint16_t gSrcPort;
static uint16_t gDstPort;

#endif

#if ENABLE_ARP_REPLY
// ARP 默认的MAC地址
static uint8_t gDefaultArpMac[RTE_ETHER_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#endif



// 默认使用的是eth0
int gDpdkPortId = 0;


// 创建一个配置信息，包的最大长度是以太网长度。
static const struct rte_eth_conf port_conf_default = {
	.rxmode = {.max_rx_pkt_len = RTE_ETHER_MAX_LEN }
};

// 端口的初始化
static void lin_init_port(struct rte_mempool *mbuf_pool) {

	// 获取有效端口数目
	uint16_t nb_sys_ports= rte_eth_dev_count_avail(); //
	if (nb_sys_ports == 0) {
		rte_exit(EXIT_FAILURE, "No Supported eth found\n");
	}

	// 获取端口对应的设备信息
	struct rte_eth_dev_info dev_info;
	rte_eth_dev_info_get(gDpdkPortId, &dev_info); //

	// 设置接收队列和发送队列
	const int num_rx_queues = 1;
	const int num_tx_queues = 1;
	struct rte_eth_conf port_conf = port_conf_default;
	rte_eth_dev_configure(gDpdkPortId, num_rx_queues, num_tx_queues, &port_conf);

	// 把相关接收配置信息设置到端口
	if (rte_eth_rx_queue_setup(gDpdkPortId, 0 , 1024, 
		rte_eth_dev_socket_id(gDpdkPortId),NULL, mbuf_pool) < 0) {

		rte_exit(EXIT_FAILURE, "Could not setup RX queue\n");

	}
	
#if ENABLE_SEND
	// 把相关的发送配置设置到端口
	struct rte_eth_txconf txq_conf = dev_info.default_txconf;
	txq_conf.offloads = port_conf.rxmode.offloads;
	if (rte_eth_tx_queue_setup(gDpdkPortId, 0 , 1024, 
		rte_eth_dev_socket_id(gDpdkPortId), &txq_conf) < 0) {
		
		rte_exit(EXIT_FAILURE, "Could not setup TX queue\n");
		
	}
#endif

	// 启动这个端口
	if (rte_eth_dev_start(gDpdkPortId) < 0 ) {
		rte_exit(EXIT_FAILURE, "Could not start\n");
	}

	

}

// udp 数据包的生成
#ifdef ENABLE_UDP
static int lin_encode_udp_pkt(uint8_t *msg, unsigned char *data, uint16_t total_len) {

	// encode 
	// 1 ethhdr 以太网头部信息的填写
	struct rte_ether_hdr *eth = (struct rte_ether_hdr *)msg;
	rte_memcpy(eth->s_addr.addr_bytes, gSrcMac, RTE_ETHER_ADDR_LEN);
	rte_memcpy(eth->d_addr.addr_bytes, gDstMac, RTE_ETHER_ADDR_LEN);
	eth->ether_type = htons(RTE_ETHER_TYPE_IPV4);
	
	// 2 iphdr ip头部信息的填写
	struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(msg + sizeof(struct rte_ether_hdr));
	ip->version_ihl = 0x45;
	ip->type_of_service = 0;
	ip->total_length = htons(total_len - sizeof(struct rte_ether_hdr));
	ip->packet_id = 0;
	ip->fragment_offset = 0;
	ip->time_to_live = 64; // ttl = 64
	ip->next_proto_id = IPPROTO_UDP;
	ip->src_addr = gSrcIp;
	ip->dst_addr = gDstIp;
	
	ip->hdr_checksum = 0;
	ip->hdr_checksum = rte_ipv4_cksum(ip);

	// 3 udphdr 头部信息的填写和数据
	struct rte_udp_hdr *udp = (struct rte_udp_hdr *)(msg + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
	udp->src_port = gSrcPort;
	udp->dst_port = gDstPort;
	uint16_t udplen = total_len - sizeof(struct rte_ether_hdr) - sizeof(struct rte_ipv4_hdr);
	udp->dgram_len = htons(udplen);

	rte_memcpy((uint8_t*)(udp+1), data, udplen);

	// 计算udp字段里面校验和
	udp->dgram_cksum = 0;
	udp->dgram_cksum = rte_ipv4_udptcp_cksum(ip, udp);

	// 地址打印验证
	struct in_addr addr;
	addr.s_addr = gSrcIp;
	printf(" --> src: %s:%d, ", inet_ntoa(addr), ntohs(gSrcPort));

	addr.s_addr = gDstIp;
	printf("dst: %s:%d\n", inet_ntoa(addr), ntohs(gDstPort));

	return 0;
}

// udp 数据包的发送
static struct rte_mbuf * lin_send_udp(struct rte_mempool *mbuf_pool, uint8_t *data, uint16_t length) {

	// mempool --> mbuf
	
	const unsigned total_len = length + 42;

	// 从内存池里面获取一个mbuf
	struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mbuf_pool);
	if (!mbuf) {
		rte_exit(EXIT_FAILURE, "rte_pktmbuf_alloc\n");
	}
	// 设置包的总长度和数据的总长度
	mbuf->pkt_len = total_len;
	mbuf->data_len = total_len;

	uint8_t *pktdata = rte_pktmbuf_mtod(mbuf, uint8_t*);

	lin_encode_udp_pkt(pktdata, data, total_len);

	return mbuf;

}

#endif

#if ENABLE_ARP
// ARP 广播包生成
static int lin_encode_arp_pkt(uint8_t *msg, uint16_t opcode, uint8_t *dst_mac, uint32_t sip, uint32_t dip) {

	// 1 ethhdr 填写以太网头部信息
	struct rte_ether_hdr *eth = (struct rte_ether_hdr *)msg;
	rte_memcpy(eth->s_addr.addr_bytes, gSrcMac, RTE_ETHER_ADDR_LEN);
	rte_memcpy(eth->d_addr.addr_bytes, dst_mac, RTE_ETHER_ADDR_LEN);
	eth->ether_type = htons(RTE_ETHER_TYPE_ARP);

	// 2 arp 填写arp头部信息
	struct rte_arp_hdr *arp = (struct rte_arp_hdr *)(eth + 1);
	arp->arp_hardware = htons(1);
	arp->arp_protocol = htons(RTE_ETHER_TYPE_IPV4);
	arp->arp_hlen = RTE_ETHER_ADDR_LEN;
	arp->arp_plen = sizeof(uint32_t);
	arp->arp_opcode = htons(opcode);

	rte_memcpy(arp->arp_data.arp_sha.addr_bytes, gSrcMac, RTE_ETHER_ADDR_LEN);
	//rte_memcpy( arp->arp_data.arp_tha.addr_bytes, dst_mac, RTE_ETHER_ADDR_LEN);
	// 填写目标MAC地址，根据接收的数据包里面的mac地址的不同，分为自己发送请求或者是返回包
	// arp请求包
	if (!strncmp((const char *)dst_mac, (const char *)gDefaultArpMac, RTE_ETHER_ADDR_LEN)) {
                uint8_t mac[RTE_ETHER_ADDR_LEN] = {0x0};
                rte_memcpy(arp->arp_data.arp_tha.addr_bytes, mac, RTE_ETHER_ADDR_LEN);

        } else {
                rte_memcpy(arp->arp_data.arp_tha.addr_bytes, dst_mac, RTE_ETHER_ADDR_LEN);
        }
	
	arp->arp_data.arp_sip = sip;
	arp->arp_data.arp_tip = dip;
	
	return 0;

}

// arp数据包发送
static struct rte_mbuf *lin_send_arp(struct rte_mempool *mbuf_pool, uint16_t opcode, uint8_t *dst_mac, uint32_t sip, uint32_t dip) {

	// 计算整个数据包的长度
	const unsigned total_length = sizeof(struct rte_ether_hdr) + sizeof(struct rte_arp_hdr);

	// 从mbuf_pool里面申请一个mbuf使用
	struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mbuf_pool);
	if (!mbuf) {
		rte_exit(EXIT_FAILURE, "rte_pktmbuf_alloc\n");
	}

	// 设置数据包的长度和数据的长度
	mbuf->pkt_len = total_length;
	mbuf->data_len = total_length;

	uint8_t *pkt_data = rte_pktmbuf_mtod(mbuf, uint8_t *);
	lin_encode_arp_pkt(pkt_data, opcode, dst_mac, sip, dip);

	return mbuf;
}

#endif


#if ENABLE_ICMP

// icmp的checksum计算
static uint16_t lin_checksum(uint16_t *addr, int count) {

	register long sum = 0;

	while (count > 1) {

		sum += *(unsigned short*)addr++;
		count -= 2;
	
	}

	if (count > 0) {
		sum += *(unsigned char *)addr;
	}

	while (sum >> 16) {
		sum = (sum & 0xffff) + (sum >> 16);
	}

	return ~sum;
}

// icmp数据包的生成
static int lin_encode_icmp_pkt(uint8_t *msg, uint8_t *dst_mac,
		uint32_t sip, uint32_t dip, uint16_t id, uint16_t seqnb) {

	// 1 ether 以太网头部信息填写
	struct rte_ether_hdr *eth = (struct rte_ether_hdr *)msg;
	rte_memcpy(eth->s_addr.addr_bytes, gSrcMac, RTE_ETHER_ADDR_LEN);
	rte_memcpy(eth->d_addr.addr_bytes, dst_mac, RTE_ETHER_ADDR_LEN);
	eth->ether_type = htons(RTE_ETHER_TYPE_IPV4);

	// 2 ip 头部信息填写
	struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(msg + sizeof(struct rte_ether_hdr));
	ip->version_ihl = 0x45;
	ip->type_of_service = 0;
	ip->total_length = htons(sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_icmp_hdr));
	ip->packet_id = 0;
	ip->fragment_offset = 0;
	ip->time_to_live = 64; // ttl = 64
	ip->next_proto_id = IPPROTO_ICMP;
	ip->src_addr = sip;
	ip->dst_addr = dip;
	
	ip->hdr_checksum = 0;
	ip->hdr_checksum = rte_ipv4_cksum(ip);

	// 3 icmp 的信息封装
	struct rte_icmp_hdr *icmp = (struct rte_icmp_hdr *)(msg + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
	icmp->icmp_type = RTE_IP_ICMP_ECHO_REPLY;
	icmp->icmp_code = 0;
	icmp->icmp_ident = id;
	icmp->icmp_seq_nb = seqnb;

	icmp->icmp_cksum = 0;
	icmp->icmp_cksum = lin_checksum((uint16_t*)icmp, sizeof(struct rte_icmp_hdr));

	return 0;
}

// 发送icmp数据包
static struct rte_mbuf *lin_send_icmp(struct rte_mempool *mbuf_pool, uint8_t *dst_mac,
		uint32_t sip, uint32_t dip, uint16_t id, uint16_t seqnb) {

	const unsigned total_length = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_icmp_hdr);

	struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mbuf_pool);
	if (!mbuf) {
		rte_exit(EXIT_FAILURE, "rte_pktmbuf_alloc\n");
	}

	
	mbuf->pkt_len = total_length;
	mbuf->data_len = total_length;

	uint8_t *pkt_data = rte_pktmbuf_mtod(mbuf, uint8_t *);
	lin_encode_icmp_pkt(pkt_data, dst_mac, sip, dip, id, seqnb);

	return mbuf;

}


#endif

// print mac
#if 1
static void 
print_ethaddr(const char *name, const struct rte_ether_addr *eth_addr)
{
	char buf[RTE_ETHER_ADDR_FMT_SIZE];
	rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, eth_addr);
	printf("%s%s", name, buf);
}
#endif

#if ENABLE_TIMER

// 定时功能的回调函数，定时发送arp广播，用于维护arp table
static void
arp_request_timer_cb(__attribute__((unused)) struct rte_timer *tim,
	   void *arg) {

	struct rte_mempool *mbuf_pool = (struct rte_mempool *)arg;
	int i = 0;
	for (i = 1;i <= 254;i ++) {
		// 生词255个ip地址
		uint32_t dstip = (gLocalIp & 0x00FFFFFF) | (0xFF000000 & (i << 24));

		struct in_addr addr;
		addr.s_addr = dstip;
		printf("arp ---> src: %s \n", inet_ntoa(addr));

		struct rte_mbuf *arpbuf = NULL;
		// 从arp table里面获取ip的mac地址
		uint8_t *dstmac = lin_get_dst_macaddr(dstip);
		// dstmac等于空的化话，发送广播包请求
		if (dstmac == NULL) {
			arpbuf = lin_send_arp(mbuf_pool, RTE_ARP_OP_REQUEST, gDefaultArpMac, gLocalIp, dstip);
		
		} else {
		// 否则就是发送单播arp请求
			arpbuf = lin_send_arp(mbuf_pool, RTE_ARP_OP_REQUEST, dstmac, gLocalIp, dstip);
		}

		rte_eth_tx_burst(gDpdkPortId, 0, &arpbuf, 1);
		rte_pktmbuf_free(arpbuf);
		
	}
	
}
#endif

// 主函数开始
int main(int argc, char *argv[]) {

	// 对dpdk环境初始化
	if (rte_eal_init(argc, argv) < 0) {
		rte_exit(EXIT_FAILURE, "Error with EAL init\n");
	}

	// 申请一个内存池mbuf_pool，名字叫mbuf pool， 有4096个mbuf，默认的mbuf大小，和指定的CPU
	struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("mbuf pool", NUM_MBUFS,
		0, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if (mbuf_pool == NULL) {
		rte_exit(EXIT_FAILURE, "Could not create mbuf pool\n");
	}
	
	// 初始化端口信息
	lin_init_port(mbuf_pool);

	// 获取本地端口对应的mac地址
	rte_eth_macaddr_get(gDpdkPortId, (struct rte_ether_addr *)gSrcMac);

#if ENABLE_TIMER

	// 初始化一个时间子系统
	rte_timer_subsystem_init();

	struct rte_timer arp_timer;
	rte_timer_init(&arp_timer);

	// 获取cpu频率和cpu
	uint64_t hz = rte_get_timer_hz();
	unsigned lcore_id = rte_lcore_id();
	// 设置时间和回调函数
	rte_timer_reset(&arp_timer, hz, PERIODICAL, lcore_id, arp_request_timer_cb, mbuf_pool);

#endif
	// 解析数据
	while (1) {
		// 声明一个mbufs指针
		struct rte_mbuf *mbufs[BURST_SIZE];
		// 从接收队列0中获取一个数据包
		unsigned num_recvd = rte_eth_rx_burst(gDpdkPortId, 0, mbufs, BURST_SIZE);
		if (num_recvd > BURST_SIZE) {
			rte_exit(EXIT_FAILURE, "Error receiving from eth\n");
		}
		// 循环遍历全部的数据包
		unsigned i = 0;
		for (i = 0;i < num_recvd;i ++) {

			// 获取以太网头部信息
			struct rte_ether_hdr *ehdr = rte_pktmbuf_mtod(mbufs[i], struct rte_ether_hdr*);

#if ENABLE_ARP
			// 判断下一层协议是不是arp协议
			if (ehdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP)) {
				// 获取arp协议的头部地址
				struct rte_arp_hdr *ahdr = rte_pktmbuf_mtod_offset(mbufs[i], 
					struct rte_arp_hdr *, sizeof(struct rte_ether_hdr));

				// 验证地址
				struct in_addr addr;
				addr.s_addr = ahdr->arp_data.arp_sip;
				printf("arp ---> src: %s ", inet_ntoa(addr));

				addr.s_addr = gLocalIp;
				printf(" local: %s \n", inet_ntoa(addr));

				// 判断arp里面的ip是不是我的
				if (ahdr->arp_data.arp_tip == gLocalIp) {
					// 判断arp协议是请求还是返回
					if (ahdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REQUEST)) {

						printf("arp --> request\n");
						// 生成一个arp 请求数据包
						struct rte_mbuf *arpbuf = lin_send_arp(mbuf_pool, RTE_ARP_OP_REPLY, ahdr->arp_data.arp_sha.addr_bytes, 
							ahdr->arp_data.arp_tip, ahdr->arp_data.arp_sip);
						// 通过tx0队列从网口发送出去
						rte_eth_tx_burst(gDpdkPortId, 0, &arpbuf, 1);
						rte_pktmbuf_free(arpbuf);

					} 
					// 判断是不是arp 回复请求
					else if (ahdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REPLY)) {
#if 1
						printf("arp --> reply\n");
						// 获取arp table
						struct arp_table *table = arp_table_instance();
						// 从arp table中获取目标的mac地址
						uint8_t *hwaddr = lin_get_dst_macaddr(ahdr->arp_data.arp_sip);
						// 没有
						if (hwaddr == NULL) {
							struct arp_entry *entry = rte_malloc("arp_entry",sizeof(struct arp_entry), 0);
							if (entry) {
								memset(entry, 0, sizeof(struct arp_entry));

								entry->ip = ahdr->arp_data.arp_sip;
								rte_memcpy(entry->hwaddr, ahdr->arp_data.arp_sha.addr_bytes, RTE_ETHER_ADDR_LEN);
								entry->type = 0;
								// 把这个ip 和 mac加入到arp table中。
								LL_ADD(entry, table->entries);
								table->count ++;
							}

						}
#endif
#if ENABLE_DEBUG
						// 调试打印
						struct arp_entry *iter;
						printf("arp_tabel:\n");
						for (iter = table->entries; iter != NULL; iter = iter->next) {
					
							struct in_addr addr;
							addr.s_addr = iter->ip;

							print_ethaddr("arp table --> mac: ", (struct rte_ether_addr *)iter->hwaddr);
								
							printf(" ip: %s \n", inet_ntoa(addr));
					
						}
#endif
						rte_pktmbuf_free(mbufs[i]);
					}
				
					continue;
				} 
			}
#endif
			// 判断下一层类型是不是IPv4
			if (ehdr->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)) {
				continue;
			}
			// 获取ip头部基地址
			struct rte_ipv4_hdr *iphdr =  rte_pktmbuf_mtod_offset(mbufs[i], struct rte_ipv4_hdr *, 
				sizeof(struct rte_ether_hdr));

			// 判断下一层是不是udp协议
			if (iphdr->next_proto_id == IPPROTO_UDP) {
				// 获取udp头部的基地址
				struct rte_udp_hdr *udphdr = (struct rte_udp_hdr *)(iphdr + 1);

#if ENABLE_SEND // 把数据包中的源ip和mac以及目的ip和mac填写到本地变量中
				
				rte_memcpy(gDstMac, ehdr->s_addr.addr_bytes, RTE_ETHER_ADDR_LEN);
				
				rte_memcpy(&gSrcIp, &iphdr->dst_addr, sizeof(uint32_t));
				rte_memcpy(&gDstIp, &iphdr->src_addr, sizeof(uint32_t));

				rte_memcpy(&gSrcPort, &udphdr->dst_port, sizeof(uint16_t));
				rte_memcpy(&gDstPort, &udphdr->src_port, sizeof(uint16_t));
#endif
				// 获取udp数据包的长度
				uint16_t length = ntohs(udphdr->dgram_len);
				*((char*)udphdr + length) = '\0';
				// 地址验证
				struct in_addr addr;
				addr.s_addr = iphdr->src_addr;
				printf("src: %s:%d, ", inet_ntoa(addr), ntohs(udphdr->src_port));

				addr.s_addr = iphdr->dst_addr;
				printf("dst: %s:%d, %s\n", inet_ntoa(addr), ntohs(udphdr->dst_port), 
					(char *)(udphdr+1));

#if ENABLE_SEND
				// 生成要发送的udp包
				struct rte_mbuf *txbuf = lin_send_udp(mbuf_pool, (uint8_t *)(udphdr+1), length);
				rte_eth_tx_burst(gDpdkPortId, 0, &txbuf, 1);
				rte_pktmbuf_free(txbuf);
				
#endif

				rte_pktmbuf_free(mbufs[i]);
			}

#if ENABLE_ICMP
			// 判断ip下一层是不是icmp协议
			if (iphdr->next_proto_id == IPPROTO_ICMP) {

				// 获取icmp的头部基础地址
				struct rte_icmp_hdr *icmphdr = (struct rte_icmp_hdr *)(iphdr + 1);

				// 验证地址
				struct in_addr addr;
				addr.s_addr = iphdr->src_addr;
				printf("icmp ---> src: %s ", inet_ntoa(addr));

				// 判断接收的icmp类型是不是请求类型
				if (icmphdr->icmp_type == RTE_IP_ICMP_ECHO_REQUEST) {

					addr.s_addr = iphdr->dst_addr;
					printf(" local: %s , type : %d\n", inet_ntoa(addr), icmphdr->icmp_type);
				
					// 生成icmp回馈的包
					struct rte_mbuf *txbuf = lin_send_icmp(mbuf_pool, ehdr->s_addr.addr_bytes,
						iphdr->dst_addr, iphdr->src_addr, icmphdr->icmp_ident, icmphdr->icmp_seq_nb);
					// 发送出去
					rte_eth_tx_burst(gDpdkPortId, 0, &txbuf, 1);
					rte_pktmbuf_free(txbuf);

					rte_pktmbuf_free(mbufs[i]);
				}
			}
#endif
			
		}

#if ENABLE_TIMER
		// 计时器
		static uint64_t prev_tsc = 0, cur_tsc;
		uint64_t diff_tsc;

		cur_tsc = rte_rdtsc();
		diff_tsc = cur_tsc - prev_tsc;
		if (diff_tsc > TIMER_RESOLUTION_CYCLES) {
			rte_timer_manage();
			prev_tsc = cur_tsc;
		}
#endif
	}

}




