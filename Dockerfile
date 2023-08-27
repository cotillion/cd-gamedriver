FROM debian:bullseye-slim as build-env
WORKDIR /build

RUN apt-get update && apt-get install -y gcc make bison libjson-c-dev pkg-config
COPY . .
RUN make MUD_LIB=/mud/lib BINDIR=/mud/bin

WORKDIR /build/util
RUN make

FROM debian:bullseye-slim
WORKDIR /mud

EXPOSE 3011
EXPOSE 3003
VOLUME /mud/lib

RUN apt-get update && apt-get install -y locales libjson-c5 && apt-get clean

RUN sed -i '/en_US.ISO-8859-1/s/^# //g' /etc/locale.gen && locale-gen

COPY --from=build-env /build/driver /mud/bin/driver
COPY --from=build-env /build/util/hname /mud/bin/hname
RUN chmod a+x /mud/bin/driver /mud/bin/hname

RUN adduser --disabled-password --gecos "" mud

USER mud
CMD ["/mud/bin/driver"]



