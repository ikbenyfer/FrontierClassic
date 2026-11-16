# Frontier Classic relay server - tiny static binary in an empty image.
# Builds on any CPU type (x86 or ARM NAS): docker compose up -d --build
FROM alpine:3.20 AS build
RUN apk add --no-cache g++
COPY relay.cpp /src/relay.cpp
RUN g++ -std=c++11 -O2 -static -s -o /relay /src/relay.cpp

FROM scratch
COPY --from=build /relay /relay
EXPOSE 47800
USER 65534:65534
ENTRYPOINT ["/relay"]
