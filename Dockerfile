# https://hub.docker.com/_/gcc
# FROM gcc:13  -- defaults to amd64 on this machine
FROM arm64v8/gcc:13
RUN mkdir -p /usr/src/myapp
COPY golden/function.c /usr/src/myapp
WORKDIR /usr/src/myapp
RUN gcc -S function.c
CMD ["cat", "function.s"]
