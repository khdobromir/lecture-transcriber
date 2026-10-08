FROM ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake g++ git make python3 python3-venv curl file binutils \
    patchelf desktop-file-utils ffmpeg shellcheck fonts-dejavu-core \
    libgl-dev libglx-dev libopengl-dev libegl-dev libxkbcommon0 libxkbcommon-x11-0 \
    libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libdbus-1-3 libfontconfig1 libfreetype6 libssl3t64 \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/aqt && /opt/aqt/bin/pip install --no-cache-dir aqtinstall==3.3.0 \
    && /opt/aqt/bin/python -m aqt install-qt linux desktop 6.8.3 linux_gcc_64 --outputdir /opt/Qt
