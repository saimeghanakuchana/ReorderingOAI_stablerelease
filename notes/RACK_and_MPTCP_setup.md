# TCP RACK and MPTCP experimental setup 

This file contains the set of instructions for packet spraying to test the throughput and latency using TCP RACK and MPTCP. The overall end-to-end setup is shown the following figure. 
---

![End to End setup](images/6G_summit.jpg)

---

This file contains the following sections 
1. Bringing up 2 UEs using namespaces 
2. Instantiating header en/decapsulating gre tunnels inside both namespaces and external data network container 
3. Set up Iperf end points 
4. Setup Routing tables and packet spraying logic 
5. Instructions for running TCP RACK among the IPERF end-points 
6. Instructions for running MPTCP among IPERF end-points 
---


Our setup with IP addresses and tunnels is shown for reference in

![End to End setup](images/overall_setup.png)

![End to End setup](images/routing_tables.png)


Bringing up 2 UEs using namespaces 
---

- 1) Create namespace for UE1 and UE2 using 
```
sudo ip netns add ue1
sudo ip netns exec ue1 ip link set lo up
sudo ip netns add ue2
sudo ip netns exec ue2 ip link set lo up

```

- 2) Connect them to the root so UEs can reach gNB listener 
```
sudo ip link add veth-main1 type veth peer name veth-ue1
sudo ip link set veth-ue1 netns ue1
sudo ip addr add 40.0.1.1/30 dev veth-main1
sudo ip link set veth-main1 up
```

```
sudo ip netns exec ue1 ip addr add 40.0.1.2/30 dev veth-ue1
sudo ip netns exec ue1 ip link set veth-ue1 up
```

```
sudo ip link add veth-main2 type veth peer name veth-ue2
sudo ip link set veth-ue2 netns ue2
sudo ip addr add 40.0.2.1/30 dev veth-main2
sudo ip link set veth-main2 up
```

```
sudo ip netns exec ue2 ip addr add 40.0.2.2/30 dev veth-ue2
sudo ip netns exec ue2 ip link set veth-ue2 up
```

- Optional: For 2 node topology, run : 
```
# for RFSIM with the two node topology
# in the UE node
sudo ip netns exec ue1 ip route add 10.10.1.1/32 via $ip_veth_main1
sudo ip netns exec ue2 ip route add 10.10.1.1/32 via $ip_veth_main2
#for RFSIM
# in the gNB node
sudo ip route add 40.0.0.0/16 via 10.10.1.2
```

- 3) Store the IP addresses of all in the following variables  
```
ip_veth_ue1=$(sudo ip netns exec ue1 ip -o -4 addr show dev veth-ue1 | awk '{print $4}' | cut -d/ -f1)
ip_veth_main1=$(ip -o -4 addr show dev veth-main1 | awk '{print $4}'  | cut -d/ -f1)
ip_veth_ue2=$(sudo ip netns exec ue2 ip -o -4 addr show dev veth-ue2 | awk '{print $4}' | cut -d/ -f1)
ip_veth_main2=$(ip -o -4 addr show dev veth-main2 | awk '{print $4}'  | cut -d/ -f1)
echo $ip_veth_ue1 $ip_veth_main1 $ip_veth_ue2 $ip_veth_main2
```

- 4) Start UE1 and UE2 (connect via veth IP)
```
sudo ip netns exec ue1 env ./ran_build/build/nr-uesoftmodem -O /local/repository/etc/ue.conf -r 106 -C 3619200000 --numerology 1 --band 78 --rfsim --rfsimulator.options chanmod --rfsimulator.serveraddr $ip_veth_main1
sudo ip netns exec ue2 env ./ran_build/build/nr-uesoftmodem -O /local/repository/etc/ue2.conf -r 106 -C 3619200000 --numerology 1 --band 78 --rfsim --rfsimulator.options chanmod --rfsimulator.serveraddr $ip_veth_main2

# If using two nodes, use: 
RFSIMULATOR=$ip_veth_main1 ./ran_build/build/nr-uesoftmodem -r 106 --numerology 1 --band 78 -C 3619200000    --rfsim --sa --nokrnmod -O /local/repository/etc/ue.conf
RFSIMULATOR=$ip_veth_main2 ./ran_build/build/nr-uesoftmodem -r 106 --numerology 1 --band 78 -C 3619200000    --rfsim --sa --nokrnmod -O /local/repository/etc/ue2.conf
```

- 5) Store the IP address of UEs and core network 
```
ip_ue1_tun=$(sudo ip netns exec ue1 ip -o -4 addr show dev oaitun_ue1 | awk '{print $4}' | cut -d/ -f1)
ip_ue2_tun=$(sudo ip netns exec ue2 ip -o -4 addr show dev oaitun_ue1 | awk '{print $4}' | cut -d/ -f1)
echo $ip_ue1_tun $ip_ue2_tun
ip_cdn=$(sudo docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' oai-ext-dn)
echo $ip_cdn
```
---

Instantiating header en/decapsulating gre tunnels inside both namespaces and external data network container 
---
- 6) instantiate header en/decapsulating gre tunnels inside both namespaces
```
sudo ip netns exec ue1 ip tunnel add gre1 mode gre local $ip_ue1_tun remote $ip_cdn ttl 255
sudo ip netns exec ue1 ip link set gre1 up
sudo ip netns exec ue1 ip link set gre1  mtu 1476

sudo ip netns exec ue2 ip tunnel add gre2 mode gre local $ip_ue2_tun remote $ip_cdn ttl 255
sudo ip netns exec ue2 ip link set gre2 up
sudo ip netns exec ue2 ip link set gre2  mtu 1476
```

```
# Optional: after restart 
sudo ip netns exec ue1 ip tunnel change gre1 mode gre local $ip_ue1_tun remote $ip_cdn ttl 255
sudo ip netns exec ue1 ip link set gre1 up
sudo ip netns exec ue1 ip link set gre1  mtu 1476
sudo ip netns exec ue2 ip tunnel change gre2 mode gre local $ip_ue2_tun remote $ip_cdn ttl 255
sudo ip netns exec ue2 ip link set gre2 up
sudo ip netns exec ue2 ip link set gre2  mtu 1476

```

- 7) instantiate header en/decapsulating gre tunnel counterparts at the core data network container
```
sudo docker exec -it oai-ext-dn ip tunnel add gre1 mode gre local $ip_cdn remote $ip_ue1_tun ttl 255
sudo docker exec -it oai-ext-dn ip link set gre1 up
sudo docker exec -it oai-ext-dn ip link set gre1  mtu 1476
sudo docker exec -it oai-ext-dn ip tunnel add gre2 mode gre local $ip_cdn remote $ip_ue2_tun ttl 255
sudo docker exec -it oai-ext-dn ip link set gre2 up
sudo docker exec -it oai-ext-dn ip link set gre2  mtu 1476
```

```
# restart - # instantiate header en/decapsulating gre tunnel counterparts at the core data network container
sudo docker exec -it oai-ext-dn ip tunnel change gre1 mode gre local $ip_cdn remote $ip_ue1_tun ttl 255
sudo docker exec -it oai-ext-dn ip link set gre1 up
sudo docker exec -it oai-ext-dn ip link set gre1  mtu 1476
sudo docker exec -it oai-ext-dn ip tunnel change gre2 mode gre local $ip_cdn remote $ip_ue2_tun ttl 255
sudo docker exec -it oai-ext-dn ip link set gre2 up
sudo docker exec -it oai-ext-dn ip link set gre2  mtu 1476
```

```
#### IMPORTANT NOTE: if the UE crashes for some reason, the restarted ue gets a new ip adress for its tun
## in that case run the following to refresh variable and the dest adress used in the core data network the gre tunnel
# ip_ue1_tun=$(sudo ip netns exec ue1 ip -o -4 addr show dev oaitun_ue1 | awk '{print $4}' | cut -d/ -f1)
# ip_ue2_tun=$(sudo ip netns exec ue2 ip -o -4 addr show dev oaitun_ue1 | awk '{print $4}' | cut -d/ -f1)
#sudo ip netns exec ue1 ip tunnel change gre1 mode gre local $ip_ue1_tun remote $ip_cdn ttl 255
#sudo ip netns exec ue2 ip tunnel change gre2 mode gre local $ip_ue2_tun remote $ip_cdn ttl 255
# sudo docker exec -it oai-ext-dn ip tunnel change gre1 mode gre local $ip_cdn remote $ip_ue1_tun ttl 255
# sudo docker exec -it oai-ext-dn ip tunnel change gre2 mode gre local $ip_cdn remote $ip_ue2_tun ttl 255
```
---

Set up Iperf end points 
---

- 8) make sure the iperf endpoints "own" their ip addresses 
```
sudo ip addr add 30.30.30.1/32 dev lo
sudo docker exec -it oai-ext-dn ip addr add 30.30.30.2/32 dev lo
sudo docker exec -it oai-ext-dn ip addr add 40.1.0.2/32 dev lo
#  set routes from ue namespaces to the root
sudo ip netns exec ue1 ip route add 30.30.30.1/32 via $ip_veth_main1
sudo ip netns exec ue2 ip route add 30.30.30.1/32 via $ip_veth_main2
#  forward uplink packets to the encapsulating tunnels inside the namespaces
sudo ip netns exec ue1 ip route add 30.30.30.2/32 dev gre1
sudo ip netns exec ue2 ip route add 30.30.30.2/32 dev gre2
sudo ip netns exec ue1 ip route add 40.1.0.2/32 dev gre1
sudo ip netns exec ue2 ip route add 40.1.0.2/32 dev gre2

```
---

Setup Routing tables and packet spraying logic
---

- 9) set up routing tables to be used in round robin forwarding in the core data network container
```
sudo docker exec -it oai-ext-dn ip route add 30.30.30.1/32 dev gre1 src 30.30.30.2
#sudo docker exec -it oai-ext-dn ip route add 30.30.30.0/31 dev gre2 src 30.30.30.2  # "fake entry" so that recv'd packets are not dropped
sudo docker exec -it oai-ext-dn ip route add 30.30.30.1/32 dev gre2 src 30.30.30.2 table 200
sudo docker exec -it oai-ext-dn ip route add 40.0.1.1/32 dev gre1 src 40.1.0.2
sudo docker exec -it oai-ext-dn ip route add 40.0.2.1/32 dev gre2 src 40.1.0.2 

```

- 10) Packet spraying at external data network 
```
n_packets_core=2
sudo docker exec -it oai-ext-dn iptables -t mangle -F
sudo docker exec -it oai-ext-dn iptables -A OUTPUT -m statistic --mode nth --every $n_packets_core --packet 0 -t mangle --destination 30.30.30.1/32 --source 30.30.30.2/32 -j MARK --set-mark 0x3
sudo docker exec -it oai-ext-dn iptables -A OUTPUT -m mark --mark 0x3 -t mangle -j RETURN
sudo docker exec -it oai-ext-dn ip rule add fwmark 0x3 lookup 200

```

- 11) set up routing tables to be used in round robin forwarding at the root
```
sudo ip route add 30.30.30.2/32 via $ip_veth_ue1 src 30.30.30.1
#sudo ip route add 30.30.30.0/31 via $ip_veth_ue2 src 30.30.30.1 # "fake entry" so that recv'd packets are not dropped
sudo ip route add 30.30.30.2/32 via $ip_veth_ue2 src 30.30.30.1 table 100
#TODO 40.1 tables
#sudo ip route add 40.1.0.0/31 via $ip_veth_ue1 src 40.0.1.1 # "fake entry" so that recv'd packets are not dropped # not sure if needed
sudo ip route add 40.1.0.2/32 via 40.0.1.2 dev veth-main1 src 40.0.1.1 table 101
sudo ip route add 40.1.0.2/32 via 40.0.2.2 dev veth-main2 src 40.0.2.1 table 102
# rules: if source is 10.0.1.2 use table A_ifA1; if source is 10.0.2.2 use table A_ifA2
sudo ip rule add from 40.0.1.1/32 to 40.1.0.2/32 table 101
sudo ip rule add from 40.0.2.1/32 to 40.1.0.2/32 table 102
```

- 12) Packet spraying at the root 
```
n_packets_ue=2
sudo iptables -t mangle -F
sudo iptables -A OUTPUT -m statistic --mode nth --every $n_packets_ue --packet 0 -t mangle --destination 30.30.30.2/32 --source 30.30.30.1/32 -j MARK --set-mark 0x3
sudo iptables -A OUTPUT -m mark --mark 0x3 -t mangle -j RETURN
# send marked packets to the other table
sudo ip rule add fwmark 0x3 lookup 100

```

- 13) use the most relaxed reverse path filtering rules so that our packets coming over different gres are not dropped

```
sudo docker exec oai-ext-dn sysctl -w net.ipv4.conf.all.rp_filter=0
sudo docker exec oai-ext-dn sysctl -w net.ipv4.conf.gre1.rp_filter=0
sudo docker exec oai-ext-dn sysctl -w net.ipv4.conf.gre2.rp_filter=0
sudo docker exec oai-ext-dn ip route flush cache
sudo ip netns exec ue1 sysctl -w net.ipv4.conf.all.rp_filter=0
sudo ip netns exec ue1 sysctl -w net.ipv4.conf.gre1.rp_filter=0
sudo ip netns exec ue1 ip route flush cache
sudo ip netns exec ue2 sysctl -w net.ipv4.conf.all.rp_filter=0
sudo ip netns exec ue2 sysctl -w net.ipv4.conf.gre2.rp_filter=0
sudo ip netns exec ue2 ip route flush cache
sudo ip mptcp limits set subflow 8 add_addr_accepted 8
sudo ip mptcp endpoint flush
sudo ip mptcp endpoint add 40.0.1.1 dev veth-main1 id 1 signal subflow
sudo ip mptcp endpoint add 40.0.2.1 dev veth-main2 id 2 signal subflow
sudo ip mptcp endpoint show
sudo docker exec oai-ext-dn ip mptcp limits set subflow 8 add_addr_accepted 8
```

---

Instructions for running TCP RACK among the IPERF end-points 
---

- 14) RACK testing 

```
# RACK
sudo docker exec oai-ext-dn ping -c 4 30.30.30.1 -I 30.30.30.2
sudo docker exec oai-ext-dn iperf3 -s -B 30.30.30.2
iperf3 -c 30.30.30.2 -B 30.30.30.1 -t 60 -R 
```

```
# Previous RACK commands 
# to test iperf over multi-path
sudo docker exec oai-ext-dn ping 30.30.30.1 -I 30.30.30.2
iperf3 -s -B 30.30.30.1
sudo docker exec oai-ext-dn iperf3 -c 30.30.30.1 -B 30.30.30.2
```

```
# to test pinging with multipath spraying
ping -c 4 30.30.30.2 -I 30.30.30.1
or
sudo docker exec oai-ext-dn ping -c 4 30.30.30.1 -I 30.30.30.2
# to test iperf over multi-path
sudo docker exec oai-ext-dn iperf3 -c 30.30.30.1 -B 30.30.30.2
iperf3 -s -B 30.30.30.1
# or in reverse
sudo docker exec oai-ext-dn iperf3 -s -B 30.30.30.2
iperf3 -c 30.30.30.2 -B 30.30.30.1 -R
```

```
# to test iperf over single paths
sudo ip netns exec ue1 iperf3 -s -B $ip_ue1_tun
sudo docker exec oai-ext-dn iperf3 -c $ip_ue1_tun
#or
sudo ip netns exec ue2 iperf3 -s -B $ip_ue2_tun
sudo docker exec oai-ext-dn iperf3 -c $ip_ue2_tun
# tcpdump captures at ue1 or ue2 tuns
sudo ip netns exec ue1 tcpdump -ni oaitun_ue1
sudo ip netns exec ue2 tcpdump -ni oaitun_ue2

```

---

Instructions for running MPTCP among IPERF end-points 
---

MPTCP setup: 

```
#for mptcp
sudo docker exec oai-ext-dn apt update
sudo docker exec oai-ext-dn apt install -y iproute2 iperf3 mptcpd
#ue side
sudo apt update
sudo apt install -y iproute2 iperf3 mptcpd   # provides mptcpize; kernel does the MPTCP
```

Testing MPTCP: 
```
#(Reverse mode)  
# to test mptcp 
sudo docker exec oai-ext-dn ping -c 4 40.0.1.1 -I 40.1.0.2
sudo docker exec oai-ext-dn mptcpize run iperf3 -s -B 40.1.0.2
sudo mptcpize run iperf3 -c 40.1.0.2 -B 40.0.1.1 -t 60 -R
```

---

Other Commands:
---
```
# RLC buffer size modification: openairinterface5g_modified/common/platform_constants.h
// RLC Entity
#define RLC_TX_MAXSIZE       10000000
#define RLC_RX_MAXSIZE       10000000
sudo chrt -f 90 taskset -c 2-6 sudo RFSIMULATOR=server ./ran_build/build/nr-softmodem -O /local/repository/etc/gnb.conf --sa --rfsim
sudo chrt -f 80 taskset -c 7-9 ip netns exec ue1 env RFSIMULATOR=$ip_veth_main1 ./ran_build/build/nr-uesoftmodem -r 106 --numerology 1 --band 78 -C 3619200000    --rfsim --sa --nokrnmod -O /local/repository/etc/ue.conf
sudo chrt -f 81 taskset -c 10-12 ip netns exec ue2 env RFSIMULATOR=$ip_veth_main2 ./ran_build/build/nr-uesoftmodem -r 106 --numerology 1 --band 78 -C 3619200000    --rfsim --sa --nokrnmod -O /local/repository/etc/ue2.conf
Grow socket buffers (caps)
# host-wide caps for UDP/TCP buffers
sudo sysctl -w net.core.rmem_max=268435456
sudo sysctl -w net.core.wmem_max=268435456
sudo sysctl -w net.core.rmem_default=134217728
sudo sysctl -w net.core.wmem_default=134217728
sudo sysctl -w net.core.netdev_max_backlog=250000
sudo sysctl -w net.ipv4.udp_mem="67108864 134217728 268435456"
sudo sysctl -w net.ipv4.udp_rmem_min=262144
sudo sysctl -w net.ipv4.udp_wmem_min=262144

ps aux | grep nr

```








