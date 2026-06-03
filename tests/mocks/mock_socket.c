/*
MIT License

Copyright (c) 2017 neural75

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include "mock_socket.h"
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BUFSIZE 1024
#define RESPONSE_QUEUE_MAX 16

static char last_command[BUFSIZE] = {0};
static char response_queue[RESPONSE_QUEUE_MAX][BUFSIZE];
static int response_queue_tail = 0;
static int response_queue_count = 0;
static bool mock_enabled = false;

ssize_t __real_write(int fd, const void *buf, size_t count);
ssize_t __real_read(int fd, void *buf, size_t count);
int __real_socket(int domain, int type, int protocol);
struct hostent* __real_gethostbyname(const char *name);
int __real_connect(int fd, const struct sockaddr *addr, socklen_t len);

static char actual_hostname[BUFSIZE] = {0};
static int actual_portno = 0;

void mock_socket_reset(void)
{
    mock_enabled = true;
    memset(last_command, 0, BUFSIZE);
    memset(actual_hostname, 0, BUFSIZE);
    actual_portno = 0;
    memset(response_queue, 0, sizeof(response_queue));
    response_queue_tail = 0;
    response_queue_count = 0;
}

static void enqueue(const char *response)
{
    if (response_queue_count >= RESPONSE_QUEUE_MAX)
        return;
    strncpy(response_queue[response_queue_tail], response, BUFSIZE - 1);
    response_queue_tail = (response_queue_tail + 1) % RESPONSE_QUEUE_MAX;
    response_queue_count++;
}

void mock_socket_set_response(const char *response)
{
    memset(response_queue, 0, sizeof(response_queue));
    response_queue_tail = 0;
    response_queue_count = 0;
    if (!mock_enabled)
        mock_enabled = true;
    if (response)
        enqueue(response);
}

void mock_socket_add_response(const char *response)
{
    if (response)
        enqueue(response);
}

const char* mock_socket_get_last_command(void)
{
    return last_command;
}

ssize_t __wrap_write(int fd, const void *buf, size_t count)
{
    if (!mock_enabled)
        return __real_write(fd, buf, count);

    size_t len = count < BUFSIZE - 1 ? count : BUFSIZE - 1;
    strncpy(last_command, (const char*)buf, len);
    last_command[len] = '\0';
    return count;
}

static const char* dequeue(void)
{
    if (response_queue_count <= 0)
        return NULL;

    int head = (response_queue_tail - response_queue_count + RESPONSE_QUEUE_MAX) % RESPONSE_QUEUE_MAX;
    const char *resp = response_queue[head];
    response_queue_count--;
    return resp;
}

ssize_t __wrap_read(int fd, void *buf, size_t count)
{
    if (!mock_enabled)
        return __real_read(fd, buf, count);

    const char *resp = dequeue();
    if (!resp)
        return 0;

    size_t len = strlen(resp);
    if (len > count)
        len = count;
    memcpy(buf, resp, len);
    return len;
}

const char* mock_socket_get_actual_host(void)
{
    return actual_hostname;
}

int mock_socket_get_actual_port(void)
{
    return actual_portno;
}

int __wrap_socket(int domain, int type, int protocol)
{
    if (!mock_enabled)
        return __real_socket(domain, type, protocol);
    return MOCK_SOCKFD;
}

struct hostent* __wrap_gethostbyname(const char *name)
{
    if (!mock_enabled)
        return __real_gethostbyname(name);

    strncpy(actual_hostname, name, BUFSIZE - 1);
    actual_hostname[BUFSIZE - 1] = '\0';

    static struct hostent he;
    static struct in_addr addr;
    static char *addr_list[2];
    addr.s_addr = inet_addr("127.0.0.1");
    addr_list[0] = (char*)&addr;
    addr_list[1] = NULL;
    he.h_addr_list = addr_list;
    he.h_length = sizeof(struct in_addr);
    he.h_addrtype = AF_INET;
    return &he;
}

int __wrap_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    if (!mock_enabled)
        return __real_connect(fd, addr, len);

    if (addr->sa_family == AF_INET)
    {
        const struct sockaddr_in *in = (const struct sockaddr_in*)addr;
        actual_portno = ntohs(in->sin_port);
    }
    return 0;
}
