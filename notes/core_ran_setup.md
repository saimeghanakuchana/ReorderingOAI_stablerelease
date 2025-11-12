# Core and RAN Setup

This file contains the set of instructions for bringing up an end-to-end 5G network on a single server. 
We begin by bringing up the Core Network first and the RAN later. 
---

Setting up the Core Network 
---

- First we clone the github repository for the core network from https://gitlab.eurecom.fr/oai/cn5g/oai-cn5g-fed.git 
Identify the branch you want to clone and pass it as a --branch argument as follows
We use the recent stable version of the Core Network 
```
cd /local/repository
git clone --branch v2.1.0-1.2 --single-branch https://gitlab.eurecom.fr/oai/cn5g/oai-cn5g-fed.git 

```
---

- We build the images present in the oai-cn5g-fed repository for AMF, UPF, etc. Use 
```
cd /local/repository
bash deploy-oai.sh cn

```
---

- To bring the Core Network up using these docker containers, run
```
cd /mydata/oai-cn5g/docker-compose/
sudo docker compose -f docker-compose-basic-nrf.yaml up -d

```
You should see 
```
OAI 5G Core network is configured and healthy....
```

---
- To observe the AMF logs run 
```
cd /mydata/oai-cn5g/docker-compose/
sudo docker logs -f oai-amf
```

---
- To bring down the Core Network, run 
```
cd /mydata/oai-cn5g/docker-compose/
sudo docker compose -f docker-compose-basic-nrf.yaml down

```

--- 
- Note down the AMF IP address using 
```
sudo docker inspect oai-amf | grep IPAddress
```
---

Setting up the RAN 
---

- First we clone the github repository for the core network from https://gitlab.eurecom.fr/oai/cn5g/oai-cn5g-fed.git 
Identify the branch you want to clone and pass it as a --branch argument as follows
We use the recent stable version of the RAN 

```
cd /local/repository
git clone --branch v2.3.0 --single-branch https://gitlab.eurecom.fr/oai/openairinterface5g.git 

```
---

- To build the RAN (both gNB and UE), run  
```
cd /local/repository
bash deploy-oai.sh ran

```
This will install all the dependencies and build the gNB and UE 

--- 

- To start the gNB, run 
```
cd /mydata/openairinterface5g/cmake_targets
sudo ./ran_build/build/nr-softmodem -O /local/repository/etc/gnb.conf --rfsim
```
---

- To start a UE, in a new terminal, use 
```
cd /mydata/openairinterface5g/cmake_targets
sudo ./ran_build/build/nr-uesoftmodem -O /local/repository/etc/ue.conf -r 106 -C 3619200000 --numerology 1 --band 78 --rfsim --rfsimulator.options chanmod --rfsimulator.serveraddr 127.0.0.1
```

--- 

Few comments 
---

- When we tried to start the gNB we observed that it was not registering to the Core Network due to the IP address mismatch of AMF in the core and the address of AMF in the /local/repository/etc/gNB.conf 
- So, to tackle it we first noted the IP address of the AMF by running 

```
sudo docker inspect oai-amf | grep IPAddress
```
- Now, modify the IP address of AMF in the gNB config file at  /local/repository/etc/gNB.conf 

```
NETWORK_INTERFACES->GNB_IPV4_ADDRESS_FOR_NG_AMF 
```

- ## TODO: In future, intead of manually changing the address, try to resolve by fixing the AMF ipaddress (check: /mydata/oai-cn5g/docker-compose/conf$ nano basic_nrf_config.yaml) 

-Also, change the subnet mask in the same file to 26 

```
NETWORK_INTERFACES-> GNB_IPV4_ADDRESS_FOR_NGU to 26
```

- We need to also change the following in the UE config files located at /local/repository/etc

```
#ue.conf and ue2.conf:  uicc0.dnn = "oai.ipv4"
``` 
