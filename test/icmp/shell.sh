#!/bin/bash

echo "请输入你的选择：\n"
while true
do
	echo "1......icmp"
	echo "2......udp"
	read select
	case $select in
	1)
		echo "icmp format: dst_ip"
		echo "example: 192.168.160.156"
		read dst_ip
		./icmp/ping $dst_ip
		;;
	2)	echo "udp start"
		./udpclient
		;;
	*)	
		echo "input error"
	esac
done
