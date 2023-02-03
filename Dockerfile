# To run:
#    docker run -p <exposed ip address>:<exposed port>:<container port> -v <path to mud root containing mudlib, data and default domain>:/mud/lib <docker image id>
#    podman works as well for those who want don't want a management daemon running as root
# Example:
#    docker run -p 127.0.0.1:3011:3011 -v /path/to/mudlib/mud:/mud/lib 0123456789
#    podman run -p 127.0.0.1:3011:3011 -v /path/to/mudlib/mud:/mud/lib 0123456789

FROM ubuntu AS build

RUN apt-get update && apt-get install -y gcc make bison libjson-c-dev subversion ca-certificates

RUN mkdir -p ~/.subversion
RUN echo "[global]" >> ~/.subversion/servers
RUN echo "ssl-authority-files=/etc/ssl/certs/ca-certificates.crt" >> ~/.subversion/servers
RUN mkdir -p /usr/src
WORKDIR /usr/src
ADD . ./cd-gamedriver
WORKDIR /usr/src/cd-gamedriver
RUN make SYS_LIBS="-lcrypt -ljson-c" MUD_LIB=/mud/lib BINDIR=/mud/bin
RUN make SYS_LIBS="-lcrypt -ljson-c" MUD_LIB=/mud/lib BINDIR=/mud/bin utils
RUN mkdir -p /mud/lib
RUN mkdir -p /mud/bin
RUN touch /mud/bin/driver
RUN make MUD_LIB=/mud/lib BINDIR=/mud/bin install
RUN make MUD_LIB=/mud/lib BINDIR=/mud/bin install.utils

FROM ubuntu

RUN apt-get update && apt-get install -y libjson-c5

EXPOSE 3011
COPY --from=build /mud/ /mud/
RUN chmod u+x /mud/bin/driver

RUN apt-get clean

CMD ["./mud/bin/driver"]

LABEL Name=cd-gamedriver Version=6.0.x
