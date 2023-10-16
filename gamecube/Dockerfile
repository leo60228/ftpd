FROM devkitpro/devkitppc:20230929

RUN apt-get update && apt-get install -y build-essential binutils-arm-none-eabi git libpng-dev libncurses5 python2-dev

RUN git clone https://github.com/extremscorner/libogc2.git /tmp/libogc2 && \
    cd /tmp/libogc2 && \
    make && \
    make install && \
    cd && \
    rm -rf /tmp/libogc2

RUN git clone https://github.com/extremscorner/libfat.git /tmp/libfat && \
    cd /tmp/libfat && \
    make ogc-release && \
    make ogc-install && \
    cd && \
    rm -rf /tmp/libfat
