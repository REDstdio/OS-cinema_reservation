FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y --no-install-recommends build-essential make && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# แก้ปัญหา line ending แบบ Windows (CRLF) แล้ว build
RUN sed -i 's/\r$//' Makefile *.sh *.c *.h && chmod +x *.sh && make

CMD ["bash"]
