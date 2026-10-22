FROM debian:trixie-slim AS build-env
WORKDIR /build

RUN apt-get -o Acquire::Retries=3 update --error-on=any \
    && apt-get -o Acquire::Retries=3 install -y --no-install-recommends \
        bison \
        gcc \
        libc6-dev \
        libcrypt-dev \
        libjson-c-dev \
        make \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

COPY . .
RUN make MUD_LIB=/mud/lib BINDIR=/mud/bin && make -C util

FROM debian:trixie-slim
WORKDIR /mud

RUN apt-get -o Acquire::Retries=3 update --error-on=any \
    && apt-get -o Acquire::Retries=3 install -y --no-install-recommends \
        libcrypt1 \
        libjson-c5 \
        locales \
    && sed -i '/en_US.ISO-8859-1/s/^# //g' /etc/locale.gen \
    && locale-gen \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --user-group mud

COPY --from=build-env --chmod=0755 /build/driver /mud/bin/driver
COPY --from=build-env --chmod=0755 /build/util/hname /mud/bin/hname

EXPOSE 3011 3003
VOLUME /mud/lib

USER mud
CMD ["/mud/bin/driver"]
