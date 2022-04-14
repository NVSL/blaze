FROM ubuntu:18.04 

ARG PROJECT_HOME=/home/blaze
ARG DATASET_HOME=/mnt/nvme
ARG BLAZE_BUILD_TYPE=Release
ARG NUM_CORES=16

RUN apt update
RUN apt install -y build-essential cmake git libboost-dev \
    sysstat psmisc vim python3-pip python3 google-perftools

RUN pip3 install pandas

RUN ln -s /usr/lib/x86_64-linux-gnu/libtcmalloc.so.4 /usr/lib/x86_64-linux-gnu/libtcmalloc.so

# Build Blaze
RUN mkdir -p ${PROJECT_HOME}
COPY . ${PROJECT_HOME}/
WORKDIR ${PROJECT_HOME}
RUN mkdir build && \
    cd build && cmake -DCMAKE_BUILD_TYPE=${BLAZE_BUILD_TYPE} .. && make -j${NUM_CORES}

RUN mkdir -p $DATASET_HOME
