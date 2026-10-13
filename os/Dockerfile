# Быстрый тест оболочки без сборки ISO:  docker build -f os/Dockerfile -t xrossos . 
#                                       docker run --rm -p 8077:8077 xrossos  -> http://localhost:8077
FROM alpine:3.20
RUN apk add --no-cache busybox-extras
COPY shell/ /usr/share/xross/shell/
EXPOSE 8077
CMD ["busybox", "httpd", "-f", "-p", "8077", "-h", "/usr/share/xross/shell"]
