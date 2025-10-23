
# Exit on error
set -e

NODE_ROLE=$1

if [ ! -d /mydata ]; then
    echo "Creating /mydata volume..."
    cd /
    sudo mkdir /mydata
    sudo /usr/local/etc/emulab/mkextrafs.pl -f /mydata

    # change ownership
    username=$(whoami)
    groupname=$(id -gn)
    sudo chown $username:$groupname /mydata
    chmod 775 /mydata
    ls -ld mydata
else
    echo "/mydata already exists. Skipping creation."
fi




# Define working directory for CN5G
# CN_DIR="/var/tmp/oai-cn5g"
# CN_DIR="/opt/oai-cn5g"
CORE_LOCAL_SRC="/local/repository/oai-cn5g-fed"
CN_WORKING_DIR="/mydata/oai-cn5g"
SRCDIR="/mydata"
ETCDIR="/local/repository/etc"

# Define working directory for RAN and UE 
RAN_LOCAL_SRC="/local/repository/openairinterface5g"  # Read-only repo you pre-loaded into the experiment
RAN_WORKING_DIR="$SRCDIR/openairinterface5g"              # Writable working directory, e.g., /mydata/openairinterface5g

if [ ! -d "$RAN_WORKING_DIR/.git" ]; then
    echo "Copying openairinterface5g repo to working dir..."
    mkdir -p "$RAN_WORKING_DIR"
    cp -a "$RAN_LOCAL_SRC"/* "$RAN_WORKING_DIR"
else
    echo "Repo already copied to $RAN_WORKING_DIR — skipping."
fi

# Create working folder if not already present


# Clean up any previous attempt
# sudo rm -rf "$CN_DIR"
# mkdir -p "$CN_DIR"
# cd "$CN_DIR"

# Clone the modified CN5G repo
# sudo git clone -b "$BRANCH" "$GIT_REPO" .

sudo sysctl net.ipv4.conf.all.forwarding=1
sudo iptables -P FORWARD ACCEPT

# Setup the CN node
function setup_cn_node {
    # Install docker, docker compose, wireshark/tshark
    echo setting up cn node
    sudo apt-get update && sudo apt-get install -y \
      apt-transport-https \
      ca-certificates \
      curl \
      docker.io \
      docker-compose-v2 \
      gnupg \
      lsb-release

    sudo add-apt-repository -y ppa:wireshark-dev/stable
    echo "wireshark-common wireshark-common/install-setuid boolean false" | sudo debconf-set-selections

    sudo DEBIAN_FRONTEND=noninteractive apt-get update && sudo apt-get install -y \
        wireshark \
        tshark

    sudo systemctl enable docker
    sudo usermod -aG docker $USER

    printf "installing compose"
    until sudo curl -L "https://github.com/docker/compose/releases/download/1.29.2/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose; do
        printf '.'
        sleep 2
    done

    sudo chmod +x /usr/local/bin/docker-compose

    sudo sysctl net.ipv4.conf.all.forwarding=1
    sudo iptables -P FORWARD ACCEPT

    # ignoring the COMMIT_HASH for now
    # sudo cp -r /local/repository/etc/oai/cn5g /var/tmp/oai-cn5g
    echo setting up cn node... done.
}

function setup_ran_node {
    # using `build-oai -I --install-optional-packages` results in interactive
    # prompts, so...
    echo installing supporting packages...
    sudo add-apt-repository -y ppa:ettusresearch/uhd
    sudo apt update && sudo apt install -y \
        iperf3 \
        libboost-dev \
        libforms-dev \
        libforms-bin \
        libuhd-dev \
        numactl \
        uhd-host \
        zlib1g \
        zlib1g-dev
    sudo uhd_images_downloader
    echo installing supporting packages... done.

    echo cloning and building oai ran...
    
    #cd $SRCDIR
    #git clone $OAI_RAN_REPO openairinterface5g
    #cd openairinterface5g
    #git checkout $COMMIT_HASH

                      # Copy all files from read-only repo to working folder
    cd "$RAN_WORKING_DIR"  
    
    cd cmake_targets

    ./build_oai -I
    ./build_oai -w USRP $BUILD_ARGS --ninja -C
    echo cloning and building oai ran... done.
}

function configure_nodeb {
    echo configuring nodeb...
    mkdir -p $SRCDIR/etc/oai
    cp -r $ETCDIR/oai/ran/* $SRCDIR/etc/oai/
    LANIF=`ip r | awk '/192\.168\.1\.0/{print $3}'`
    if [ ! -z $LANIF ]; then
      LANIP=`ip r | awk '/192\.168\.1\.0/{print $NF}'`
      echo LAN IFACE is $LANIF IP is $LANIP.. updating nodeb config
      find $SRCDIR/etc/oai/ -type f -exec sed -i "s/LANIF/$LANIF/" {} \;
      echo adding route to CN
      sudo ip route add 192.168.70.128/26 via 192.168.1.1 dev $LANIF
    else
      echo No LAN IFACE.. not updating nodeb config
    fi
    echo configuring nodeb... done.
}

function configure_ue {
    echo configuring ue...
    mkdir -p $SRCDIR/etc/oai
    cp -r $ETCDIR/oai/* $SRCDIR/etc/oai/
    echo configuring ue... done.
}

if [ $NODE_ROLE == "cn" ]; then
    setup_cn_node
    # Copy pre-loaded CN5G repo from /local/repository into working dir

    #mkdir -p "$CN_WORKING_DIR"
    #cp -r "$CORE_LOCAL_SRC"/* "$CN_WORKING_DIR"
    if [ ! -d "$CN_WORKING_DIR/.git" ]; then
        echo "Copying CN5G repo to working dir..."
        mkdir -p "$CN_WORKING_DIR"
        cp -r "$CORE_LOCAL_SRC"/* "$CN_WORKING_DIR"
    else
        echo "CN5G already copied to $CN_WORKING_DIR — skipping."
    fi

elif [ $NODE_ROLE == "nodeb" ]; then
    BUILD_ARGS="--gNB"
    setup_ran_node
    #configure_nodeb
elif [ $NODE_ROLE == "ue" ]; then
    BUILD_ARGS="--nrUE"
    setup_ran_node
    #configure_ue
fi

touch $SRCDIR/oai-setup-complete

#cd "$CN_DIR"

# # Optional: build step if your Docker images are custom
# # echo "Building Docker images..."
# # sudo docker compose -f docker-compose-basic-nrf.yaml build

# # Pull latest images if needed
# echo "Pulling Docker images..."
# sudo docker compose -f docker-compose-basic-nrf.yaml pull

# # Deploy CN5G
# echo "Starting CN5G Docker Compose setup..."
# sudo docker compose -f docker-compose-basic-nrf.yaml up -d

# # Show container status
# sudo docker compose -f docker-compose-basic-nrf.yaml ps

# # Show logs for AMF (optional)
# echo "Following logs for oai-amf..."
# sudo docker compose -f docker-compose-basic-nrf.yaml logs -f oai-amf
