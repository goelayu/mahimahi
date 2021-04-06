mahimahi: a web performance measurement toolkit

### Installation requirements: OpenSSL 1.1.0
Note this version of mahimahi isn't backwards compatible with older versions of openSSL

Install openSSL in the /usr/lib location or otherwise configure mahimahi with the following command
```bash
CPPFLAGS=-I/path-to-openssl-1.1.1g/include/ LDFLAGS=-L/path-to-openssl-1.1.1g/ ./configure
