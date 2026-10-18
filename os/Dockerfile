# Проверка сборки нативной оболочки (X-сервер в контейнере не нужен):
#   docker build -f os/Dockerfile -t xrossos .
#   docker run --rm xrossos            -> xross 0.2 (Swirl)
# Для GUI нужен хост с X11/XLibre:  xhost +local: && docker run -e DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix xrossos xross
FROM alpine:3.20 AS build
RUN apk add --no-cache build-base libx11-dev
COPY src/ /src/
# В контейнере CPU хоста неизвестен: generic; в образе для X550EA — make (btver2)
RUN make -C /src TARGET_CPU=generic && /src/xross --version

FROM alpine:3.20
RUN apk add --no-cache libx11 libxcb font-dejavu
COPY --from=build /src/xross /usr/bin/xross
CMD ["xross", "--version"]
