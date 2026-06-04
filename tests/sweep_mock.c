#include "mock_socket.h"
#include <unistd.h>
#include <sys/select.h>
#include <termios.h>
#include <stdio_ext.h>
#include <string.h>
#include <stddef.h>

/* ==================================================================
 * Keypress queue — used by both __wrap_kbhit and __wrap_select/__wrap_fgetc
 * ================================================================== */

#define KEY_QUEUE_MAX 64

static char key_queue[KEY_QUEUE_MAX];
static int  key_queue_count = 0;

int __real_kbhit(void);
int __real_select(int nfds, fd_set *readfds, fd_set *writefds,
                  fd_set *exceptfds, struct timeval *timeout);
int __real_fgetc(FILE *stream);
int __real_tcgetattr(int fd, struct termios *termios_p);
int __real_tcsetattr(int fd, int optional_actions,
                     const struct termios *termios_p);

void mock_add_keypress(char c)
{
    if (key_queue_count < KEY_QUEUE_MAX)
        key_queue[key_queue_count++] = c;
}

void mock_clear_keypresses(void)
{
    key_queue_count = 0;
}

/* ==================================================================
 * Wrapped functions
 * ================================================================== */

int __wrap_usleep(useconds_t usec)
{
    (void)usec;
    return 0;
}

int __wrap_select(int nfds, fd_set *readfds, fd_set *writefds,
                  fd_set *exceptfds, struct timeval *timeout)
{
    if (key_queue_count > 0 &&
        readfds && FD_ISSET(0, readfds))
    {
        if (timeout)
        {
            timeout->tv_sec  = 0;
            timeout->tv_usec = 0;
        }
        return 1;
    }

    if (readfds) FD_ZERO(readfds);

    /* simulate minimal timeout */
    if (timeout)
    {
        timeout->tv_sec  = 0;
        timeout->tv_usec = 0;
    }
    return 0;
}

int __wrap_kbhit(void)
{
    return key_queue_count > 0 ? 1 : 0;
}

int __wrap_fgetc(FILE *stream)
{
    (void)stream;
    if (key_queue_count > 0)
    {
        char c = key_queue[0];
        memmove(key_queue, key_queue + 1, key_queue_count - 1);
        key_queue_count--;
        return c;
    }
    return EOF;
}

int __wrap_tcgetattr(int fd, struct termios *termios_p)
{
    (void)fd;
    if (termios_p)
        memset(termios_p, 0, sizeof(struct termios));
    return 0;
}

int __wrap_tcsetattr(int fd, int optional_actions,
                     const struct termios *termios_p)
{
    (void)fd;
    (void)optional_actions;
    (void)termios_p;
    return 0;
}


