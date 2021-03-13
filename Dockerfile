FROM amd64/ubuntu:18.04 AS base

#If you found this docker image helpful please donate RVN to the maintainer
LABEL maintainer="RV9zdNeUTQUToZUcRp9uNF8gwH5LzDFtan"

EXPOSE 8766/tcp
EXPOSE 8767/tcp

ENV DEBIAN_FRONTEND=noninteractive

#Add ppa:bitcoin/bitcoin repository so we can install libdb4.8 libdb4.8++
RUN apt-get update && \
	apt-get install -y software-properties-common && \
	add-apt-repository ppa:bitcoin/bitcoin

#Install runtime dependencies
RUN apt-get update && \
	apt-get install -y --no-install-recommends \
	bash net-tools libminiupnpc10 \
	libevent-2.1 libevent-pthreads-2.1 \
	libdb4.8 libdb4.8++ \
	libboost-system1.65 libboost-filesystem1.65 libboost-chrono1.65 \
	libboost-program-options1.65 libboost-thread1.65 \
	libzmq5 && \
	apt-get clean

FROM base AS build

#Install build dependencies
RUN apt-get update && \
	apt-get install -y --no-install-recommends \
	bash net-tools build-essential libtool autotools-dev automake \
	pkg-config libssl-dev libevent-dev bsdmainutils python3 \
	libboost-system1.65-dev libboost-filesystem1.65-dev libboost-chrono1.65-dev \
	libboost-program-options1.65-dev libboost-test1.65-dev libboost-thread1.65-dev \
	libzmq3-dev libminiupnpc-dev libdb4.8-dev libdb4.8++-dev && \
	apt-get clean

#Build Ravencoin from source
COPY . /home/raven/build/Ravencoin/
WORKDIR /home/raven/build/Ravencoin
RUN ./autogen.sh && ./configure --disable-tests --with-gui=no && make

FROM base AS final

#Add our service account user
RUN useradd -ms /bin/bash raven && \
	mkdir /var/lib/raven && \
	chown raven:raven /var/lib/raven && \
	ln -s /var/lib/raven /home/raven/.raven && \
	chown -h raven:raven /home/raven/.raven

VOLUME /var/lib/raven

#Copy the compiled binaries from the build
COPY --from=build /home/raven/build/Ravencoin/src/ravend /usr/local/bin/ravend
COPY --from=build /home/raven/build/Ravencoin/src/raven-cli /usr/local/bin/raven-cli

WORKDIR /home/raven
USER raven

CMD /usr/local/bin/ravend -datadir=/var/lib/raven -printtoconsole -onlynet=ipv4
