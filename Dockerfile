FROM node:22-bookworm-slim AS frontend

WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci
COPY index.html tsconfig.json tsconfig.app.json tsconfig.node.json vite.config.ts ./
COPY src ./src
RUN npm run build

FROM gcc:13-bookworm AS backend

WORKDIR /build
COPY backend/src ./backend/src
RUN g++ -std=c++20 -O2 -Wall -Wextra -Werror -I backend/src \
  backend/src/main.cpp backend/src/transfer_core.cpp \
  -static-libstdc++ -static-libgcc \
  -o /usr/local/bin/p2p_server

FROM debian:bookworm-slim AS runtime

RUN apt-get update \
  && apt-get install -y --no-install-recommends nginx ca-certificates \
  && rm -rf /var/lib/apt/lists/*

COPY --from=frontend /app/dist /usr/share/nginx/html
COPY --from=backend /usr/local/bin/p2p_server /usr/local/bin/p2p_server
COPY docker/nginx.conf /etc/nginx/nginx.conf
COPY docker/entrypoint.sh /usr/local/bin/loopline-entrypoint

RUN sed -i 's/\r$//' /usr/local/bin/loopline-entrypoint \
  && chmod +x /usr/local/bin/loopline-entrypoint \
  && mkdir -p /data/received /data/sent /run/nginx

ENV P2P_BIND_HOST=0.0.0.0 \
  P2P_HTTP_PORT=8787 \
  P2P_TRANSFER_PORT=8788 \
  P2P_ALLOW_REMOTE=1 \
  P2P_RECEIVE_DIR=/data/received \
  P2P_SENT_DIR=/data/sent

EXPOSE 8080 8788
VOLUME ["/data/received", "/data/sent"]

CMD ["/bin/sh", "/usr/local/bin/loopline-entrypoint"]
