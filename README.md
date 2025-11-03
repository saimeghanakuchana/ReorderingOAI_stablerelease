This is a repository which contains all the components - Core Network and RAN 

CoreNetwork is forked from https://gitlab.eurecom.fr/oai/cn5g/oai-cn5g-fed.git (Branch: 2024.w45)
RAN is forked from https://gitlab.eurecom.fr/oai/openairinterface5g.git (Branch: 2025.w42)


Was facing UE RACH failure issue. 

# So, I  run the following: 
cd /local/repository/
rm -rf oai-cn5g-fed/
rm -rf openairinterface5g/

# Clone the following repositories
git clone --branch v2.1.0-1.2 --single-branch https://gitlab.eurecom.fr/oai/cn5g/oai-cn5g-fed.git  
git clone --branch v2.3.0 --single-branch https://gitlab.eurecom.fr/oai/openairinterface5g.git 

# Setup the core network 
cd /local/repository/
bash deploy-oai.sh cn 
cd /mydata/oai-cn5g/docker-compose/
sudo docker compose -f docker-compose-basic-nrf.yaml up -d

# Observe the AMF logs 
cd /mydata/oai-cn5g/docker-compose/
sudo docker logs -f oai-amf

#To start the gNB, we first made few changes in gnb.conf file present at /local/repository/etc/gnb.conf 
#This is because the gNB was not registering to the AMF 
#We first check the IP address of AMF using the following bash command
sudo docker inspect oai-amf | grep IPAddress
# now change the following in your gnb.conf 
amf_ip_address.ipv4 to the ip address observed in inspect command 
# change the subnet mask in NETWORK_INTERFACES->GNB_IPV4_ADDRESS_FOR_NG_AMF, NETWORK_INTERFACES-> GNB_IPV4_ADDRESS_FOR_NGU to 26
## TODO: In future, intead of manually changing the address, try to resolve by fixing the AMF ipaddress (check: /mydata/oai-cn5g/docker-compose/conf$ nano basic_nrf_config.yaml) 

# Changes needed in ue.conf and ue2.conf file present at /local/repository/etc/ 
uicc0.dnn = "oai.ipv4"
