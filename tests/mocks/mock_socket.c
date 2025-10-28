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
#include <stdio.h>

#define BUFSIZE 1024

static char mock_response[BUFSIZE] = {0};
static char last_command[BUFSIZE] = {0};
static bool initialized = false;

void mock_socket_reset(void)
{
    memset(mock_response, 0, BUFSIZE);
    memset(last_command, 0, BUFSIZE);
    initialized = true;
}

void mock_socket_set_response(const char *response)
{
    if (!initialized) {
        mock_socket_reset();
    }
    strncpy(mock_response, response, BUFSIZE - 1);
}

int mock_connect(char *hostname, int portno)
{
    if (!initialized) {
        mock_socket_reset();
    }
    return MOCK_SOCKFD;
}

bool mock_send(int sockfd, char *buf)
{
    if (!initialized) {
        mock_socket_reset();
    }
    strncpy(last_command, buf, BUFSIZE - 1);
    return true;
}

bool mock_recv(int sockfd, char *buf)
{
    if (!initialized) {
        mock_socket_reset();
    }
    strncpy(buf, mock_response, BUFSIZE - 1);
    return true;
}

const char* mock_socket_get_last_command(void)
{
    return last_command;
}
