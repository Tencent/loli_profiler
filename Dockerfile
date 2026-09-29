FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    wget \
    unzip \
    git \
    python3 \
    python3-pip \
    pkg-config \
    libgtk-3-dev \
    libx11-dev \
    libxrandr-dev \
    libxcursor-dev \
    libxi-dev \
    libudev-dev \
    libgl1-mesa-dev \
    libfreetype6-dev \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*


RUN python3 -m pip install --no-cache-dir 'cmake>=3.24,<4'

RUN wget https://dl.google.com/android/repository/android-ndk-r20-linux-x86_64.zip -O /tmp/android-ndk-r20.zip \
    && unzip /tmp/android-ndk-r20.zip -d /opt \
    && rm /tmp/android-ndk-r20.zip

ENV ANDROID_NDK_HOME=/opt/android-ndk-r20
ENV Ndk_R20_CMD=/opt/android-ndk-r20/ndk-build
ENV PATH=$PATH:$ANDROID_NDK_HOME

WORKDIR /workspace/loli_profiler

CMD ["bash"]
