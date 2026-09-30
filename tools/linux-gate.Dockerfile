# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
ARG BASE_IMAGE
FROM ${BASE_IMAGE}
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -q && apt-get install -y -q --no-install-recommends \
    ca-certificates git gnupg sudo python3 python3-venv g++ pkg-config \
    && rm -rf /var/lib/apt/lists/*
COPY deps/tools.json /opt/config/deps/tools.json
COPY tools/ /opt/config/tools/
RUN python3 -m venv /opt/dev && \
    /opt/dev/bin/python /opt/config/tools/install_build_tools.py && \
    /opt/dev/bin/python /opt/config/tools/install_build_tools.py --lint && \
    /opt/dev/bin/python /opt/config/tools/install_llvm.py --compiler
ENV PATH="/opt/dev/bin:${PATH}"
ENV PYTHONDONTWRITEBYTECODE=1
WORKDIR /source
