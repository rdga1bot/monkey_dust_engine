# monkey_dust build environment
# Містить: SDL3 + Jolt + ozz + Recast + libmonkey_dust_engine.a
# Використовується в monkey_dust CI (Підхід 2) щоб пропустити heavy compile.
#
# Розмір: ~800MB (SDL3+Jolt+ozz компільовані)
# Build time: ~15 хв один раз; далі шари кешуються Docker layer cache.

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# ── Системні пакети ───────────────────────────────────────────────────────────
RUN apt-get update -q && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build ccache git ca-certificates \
    liblua5.4-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
    libxkbcommon-dev libxkbcommon-x11-dev \
    libwayland-dev wayland-protocols \
    libegl1-mesa-dev libdrm-dev libgbm-dev \
    libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

# ── SDL3 (статично) ───────────────────────────────────────────────────────────
RUN git clone --depth=1 --branch release-3.4.8 \
      https://github.com/libsdl-org/SDL /tmp/SDL3_src && \
    cmake -S /tmp/SDL3_src -B /tmp/SDL3_build -G Ninja \
      -DSDL_STATIC=ON -DSDL_SHARED=OFF \
      -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF \
      -DSDL_X11_XSCRNSAVER=OFF -DSDL_X11_XTEST=OFF \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/opt/sdl3 && \
    cmake --build /tmp/SDL3_build --parallel $(nproc) && \
    cmake --install /tmp/SDL3_build && \
    rm -rf /tmp/SDL3_src /tmp/SDL3_build

# ── third_party (header-only + compiled) ─────────────────────────────────────
WORKDIR /opt/monkey_dust
RUN git clone --depth=1 https://github.com/skypjack/entt third_party/entt && \
    git clone --depth=1 https://github.com/g-truc/glm third_party/glm && \
    git clone --depth=1 https://github.com/google/googletest third_party/googletest && \
    git clone --depth=1 https://github.com/recastnavigation/recastnavigation third_party/recastnavigation && \
    git clone --depth=1 --branch v5.1.0 https://github.com/jrouwe/JoltPhysics third_party/JoltPhysics && \
    git clone --depth=1 https://github.com/guillaumeblanc/ozz-animation third_party/ozz-animation

# ── engine source → build libmonkey_dust_engine.a ────────────────────────────
# COPY вставляє поточний стан engine repo (CI підставляє після checkout)
COPY . /opt/monkey_dust/engine

RUN cmake -S /opt/monkey_dust/engine -B /opt/monkey_dust/engine_build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DUSE_SDL3=ON \
      -DMD_STATIC_DEPS=OFF \
      -DCMAKE_PREFIX_PATH=/opt/sdl3 && \
    ninja -C /opt/monkey_dust/engine_build monkey_dust_engine && \
    # Копіюємо артефакти у стандартне місце
    mkdir -p /opt/monkey_dust/dist/lib /opt/monkey_dust/dist/include && \
    cp /opt/monkey_dust/engine_build/libmonkey_dust_engine.a /opt/monkey_dust/dist/lib/ && \
    cp -r /opt/monkey_dust/engine/include/monkey_dust /opt/monkey_dust/dist/include/ && \
    cp /opt/sdl3/lib/libSDL3.a /opt/monkey_dust/dist/lib/ 2>/dev/null || true

# Env vars для monkey_dust CI
ENV MONKEY_DUST_ENGINE_LIB=/opt/monkey_dust/dist/lib/libmonkey_dust_engine.a
ENV MONKEY_DUST_ENGINE_INCLUDE=/opt/monkey_dust/dist/include
ENV MONKEY_DUST_THIRD_PARTY=/opt/monkey_dust/third_party
ENV CMAKE_PREFIX_PATH=/opt/sdl3
