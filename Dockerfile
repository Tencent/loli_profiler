FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    wget \
    unzip \
    git \
    openjdk-8-jdk \
    qt5-default \
    libqt5charts5-dev \
    libncurses5 \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

ENV QT5Path=/usr/lib/qt5

RUN wget https://dl.google.com/android/repository/android-ndk-r20-linux-x86_64.zip -O /tmp/android-ndk-r20.zip \
    && unzip /tmp/android-ndk-r20.zip -d /opt \
    && rm /tmp/android-ndk-r20.zip

RUN wget https://dl.google.com/android/repository/android-ndk-r16b-linux-x86_64.zip -O /tmp/android-ndk-r16b.zip \
    && unzip /tmp/android-ndk-r16b.zip -d /opt \
    && rm /tmp/android-ndk-r16b.zip

ENV ANDROID_NDK_HOME=/opt/android-ndk-r20
ENV Ndk_R20_CMD=/opt/android-ndk-r20/ndk-build
ENV Ndk_R16_CMD=/opt/android-ndk-r16b/ndk-build
ENV PATH=$PATH:$ANDROID_NDK_HOME

RUN wget https://dl.google.com/android/repository/commandlinetools-linux-6609375_latest.zip -O /tmp/cmdline-tools.zip \
    && mkdir -p /opt/cmdline-tools \
    && unzip /tmp/cmdline-tools.zip -d /opt/cmdline-tools \
    && rm /tmp/cmdline-tools.zip

ENV ANDROID_SDK_ROOT=/opt/cmdline-tools
ENV PATH=$PATH:$ANDROID_SDK_ROOT/tools/bin

RUN wget https://github.com/Kitware/CMake/releases/download/v3.18.4/cmake-3.18.4-Linux-x86_64.sh -O /tmp/cmake.sh \
    && chmod +x /tmp/cmake.sh \
    && /tmp/cmake.sh --skip-license --prefix=/usr/local \
    && rm /tmp/cmake.sh

WORKDIR /workspace/loli_profiler

CMD ["bash"]