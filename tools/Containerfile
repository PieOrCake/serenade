FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Core build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 python3-pip python3-venv python3-dev libpython3.10 \
    libgl1-mesa-glx libegl1 libxkbcommon0 libdbus-1-3 \
    libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-shape0 \
    libfontconfig1 libfreetype6 libglib2.0-0 \
    libsdl2-2.0-0 libsdl2-mixer-2.0-0 \
    binutils file wget fuse \
    && rm -rf /var/lib/apt/lists/*

# Install appimagetool
RUN wget -q "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" \
    -O /usr/local/bin/appimagetool && chmod +x /usr/local/bin/appimagetool

# Create venv and install Python deps
RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"
COPY requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir -r /tmp/requirements.txt pyinstaller

# Working directory
WORKDIR /build

# Build script run inside container
COPY build-inside-container.sh /build/build-inside-container.sh
RUN chmod +x /build/build-inside-container.sh

ENTRYPOINT ["/build/build-inside-container.sh"]
