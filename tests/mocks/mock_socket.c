#include "mock_socket.h"
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>

#define BUFSIZE 1024
#define RESPONSE_QUEUE_MAX 128
#define CARRIERS_MAX 256
#define PROFILE_EXPECTED_MAX 32

/* ==================================================================
 * Response queue (kept for backward compat with protocol-unit tests)
 * ================================================================== */

extern bool g_socket_dead;

static char last_command[BUFSIZE] = {0};
static char response_queue[RESPONSE_QUEUE_MAX][BUFSIZE];
static int  response_queue_tail = 0;
static int  response_queue_count = 0;
static bool mock_enabled = false;
static bool profile_mode = false;  /* set when a profile is loaded */

/* ==================================================================
 * Real-function declarations
 * ================================================================== */

ssize_t __real_write(int fd, const void *buf, size_t count);
ssize_t __real_read(int fd, void *buf, size_t count);
int __real_socket(int domain, int type, int protocol);
struct hostent* __real_gethostbyname(const char *name);
int __real_connect(int fd, const struct sockaddr *addr, socklen_t len);

/* ==================================================================
 * Connection tracking
 * ================================================================== */

static char actual_hostname[BUFSIZE] = {0};
static int  actual_portno = 0;

/* ==================================================================
 * Profile data
 * ================================================================== */

static freq_t carriers_freqs[CARRIERS_MAX];
static double carriers_levels[CARRIERS_MAX];
static int    carriers_count = 0;

static double profile_noise_floor = -120.0;
static double profile_squelch     = -110.0;
static freq_t profile_min_freq    = 0;
static freq_t profile_max_freq    = 0;

static freq_t profile_expect_freq[PROFILE_EXPECTED_MAX];
static freq_t profile_expect_tol[PROFILE_EXPECTED_MAX];
static int    profile_expect_count = 0;

/* Current frequency set via "F" command */
static freq_t last_set_freq = 0;

/* ==================================================================
 * Helpers
 * ================================================================== */

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

const char* mock_socket_get_actual_host(void)
{
    return actual_hostname;
}

int mock_socket_get_actual_port(void)
{
    return actual_portno;
}

void mock_socket_reset(void)
{
    mock_enabled = true;
    profile_mode = false;
    g_socket_dead = false;
    last_set_freq = 0;
    memset(last_command, 0, BUFSIZE);
    memset(actual_hostname, 0, BUFSIZE);
    actual_portno = 0;
    memset(response_queue, 0, sizeof(response_queue));
    response_queue_tail = 0;
    response_queue_count = 0;
}

/* ==================================================================
 * Profile loading
 * ================================================================== */

bool mock_load_profile(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) return false;

    carriers_count = 0;
    profile_expect_count = 0;
    profile_noise_floor = -120.0;
    profile_squelch     = -110.0;
    profile_min_freq    = 0;
    profile_max_freq    = 0;

    char line[BUFSIZE];
    while (fgets(line, sizeof(line), fp))
    {
        /* strip trailing newline */
        size_t ln = strlen(line);
        if (ln > 0 && line[ln - 1] == '\n') line[--ln] = '\0';

        /* skip empty and comments */
        if (ln == 0 || line[0] == '#') continue;

        if (sscanf(line, "NOISE_FLOOR %lf", &profile_noise_floor) == 1)
            continue;
        if (sscanf(line, "SQUELCH %lf", &profile_squelch) == 1)
            continue;
        if (sscanf(line, "MIN_FREQ %llu", (unsigned long long*)&profile_min_freq) == 1)
            continue;
        if (sscanf(line, "MAX_FREQ %llu", (unsigned long long*)&profile_max_freq) == 1)
            continue;

        freq_t f;
        double lvl;
        freq_t tol;
        /* EXPECT: <freq> <tol> or <freq> ±<tol> */
        if (strncmp(line, "EXPECT:", 7) == 0)
        {
            unsigned long long f_val = 0, tol_val = 0;
            if (sscanf(line + 7, " %llu", &f_val) == 1)
            {
                /* Find tolerance: skip first number, then any non-digit prefix */
                const char *p = line + 7;
                while (*p == ' ' || *p == '\t') p++;  /* skip leading space */
                while (*p && *p >= '0' && *p <= '9') p++;  /* skip freq digits */
                while (*p && !(*p >= '0' && *p <= '9')) p++; /* skip ± etc. */
                if (sscanf(p, "%llu", &tol_val) == 1 &&
                    profile_expect_count < PROFILE_EXPECTED_MAX)
                {
                    profile_expect_freq[profile_expect_count] = f_val;
                    profile_expect_tol[profile_expect_count] = tol_val;
                    profile_expect_count++;
                }
            }
            continue;
        }

        /* <freq>,<level> */
        char *comma = strchr(line, ',');
        if (comma)
        {
            *comma = '\0';
            if (sscanf(line, "%llu", (unsigned long long*)&f) == 1 &&
                sscanf(comma + 1, "%lf", &lvl) == 1)
            {
                if (carriers_count < CARRIERS_MAX)
                {
                    carriers_freqs[carriers_count] = f;
                    carriers_levels[carriers_count] = lvl;
                    carriers_count++;
                }
            }
        }
    }
    fclose(fp);

    mock_enabled = true;
    profile_mode = true;

    /* seed for deterministic noise */
    srand(42);

    return true;
}

/* ==================================================================
 * Noise and carrier look-up
 * ================================================================== */

/* random value in [NOISE_FLOOR - 5, NOISE_FLOOR] */
static double noise_sample(void)
{
    double r = (double)rand() / (double)RAND_MAX;
    return profile_noise_floor - r * 5.0;
}

/* carrier level with quadratic roll-off over ±5kHz radius,
 * noise otherwise (random jitter near noise floor) */
static double signal_at_freq(freq_t freq)
{
    int bw = 5000;

    for (int i = 0; i < carriers_count; i++)
    {
        freq_t dv = (freq > carriers_freqs[i])
                  ? freq - carriers_freqs[i]
                  : carriers_freqs[i] - freq;
        if (dv <= bw)
        {
            /* Normalized distance: 0 = carrier center, 1 = bandwidth edge */
            double norm_d = (double)dv / (double)bw;

            /*
             * Quadratic roll-off from carrier peak down to noise floor.
             *
             * In dBFS both values are negative (e.g. -80 peak, -120 floor).
             * The total drop from peak to floor is:
             *   drop = carrier_level - noise_floor   (always positive, e.g. 40 dB)
             *
             * At center (norm_d=0): level = carrier_level  (strongest signal)
             * At edge   (norm_d=1): level = noise_floor    (no signal)
             * In between: level = carrier_level - drop * norm_d²
             *
             * Example: carrier=-80, noise_floor=-120, drop=40:
             *   dv=0    norm_d=0.0   -80 - 40*0.00 =  -80
             *   dv=2500 norm_d=0.5   -80 - 40*0.25 =  -90
             *   dv=5000 norm_d=1.0   -80 - 40*1.00 = -120
             */
            double drop = carriers_levels[i] - profile_noise_floor;
            return carriers_levels[i] - drop * norm_d * norm_d;
        }
    }

    /* No carrier within bandwidth — random noise near the floor */
    {
        double r = (double)rand() / (double)RAND_MAX;
        return profile_noise_floor - r * 5.0;
    }
}

/* ==================================================================
 * Protocol-aware write: parse commands, populate response queue
 * ================================================================== */

static void handle_write(const char *cmd)
{
    /* store command for getter API */
    strncpy(last_command, cmd, BUFSIZE - 1);
    last_command[BUFSIZE - 1] = '\0';

    if (!profile_mode || !mock_enabled)
        return;

    /* "F <freq> \n" — set frequency */
    freq_t f;
    if (sscanf(cmd, "F %llu", (unsigned long long*)&f) == 1)
    {
        last_set_freq = f;
        enqueue("RPRT 0\n");
        /* GetCurrentFreq confirmation is handled dynamically
         * when "f" write is parsed below — do NOT pre-fill */
        return;
    }

    /* "f" or "f \n" — get current freq */
    if (cmd[0] == 'f' && (cmd[1] == '\n' || cmd[1] == '\0'))
    {
        char buf[BUFSIZE];
        snprintf(buf, sizeof(buf), "%llu\n",
                 (unsigned long long)last_set_freq);
        enqueue(buf);
        return;
    }

    /* "l SQL" or "l SQL \n" — get squelch level */
    if (strncmp(cmd, "l SQL", 5) == 0)
    {
        char buf[BUFSIZE];
        snprintf(buf, sizeof(buf), "%.1f\n", profile_squelch);
        enqueue(buf);
        return;
    }

    /* "L SQL %f \n" — set squelch level */
    if (strncmp(cmd, "L SQL", 5) == 0)
    {
        sscanf(cmd + 5, "%lf", &profile_squelch);
        enqueue("RPRT 0\n");
        return;
    }

    /* "l" or "l \n" — get signal level */
    if (cmd[0] == 'l' && (cmd[1] == '\n' || cmd[1] == '\0'))
    {
        double result = signal_at_freq(last_set_freq);
        char buf[BUFSIZE];
        snprintf(buf, sizeof(buf), "%.1f\n", result);
        enqueue(buf);
        return;
    }

    /* "U RECORD 1 \n" / "U RECORD 0 \n" */
    if (strncmp(cmd, "U RECORD", 8) == 0)
    {
        enqueue("RPRT 0\n");
        return;
    }
}

/* ==================================================================
 * Wrapped socket functions
 * ================================================================== */

ssize_t __wrap_write(int fd, const void *buf, size_t count)
{
    if (!mock_enabled || fd != MOCK_SOCKFD)
        return __real_write(fd, buf, count);

    size_t len = count < BUFSIZE - 1 ? count : BUFSIZE - 1;
    char cmd[BUFSIZE];
    strncpy(cmd, (const char*)buf, len);
    cmd[len] = '\0';

    handle_write(cmd);

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
    if (!mock_enabled || fd != MOCK_SOCKFD)
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

/* ==================================================================
 * Profile query API
 * ================================================================== */

int mock_expected_count(void)
{
    return profile_expect_count;
}

freq_t mock_expected_freq(int idx)
{
    if (idx < 0 || idx >= profile_expect_count)
        return 0;
    return profile_expect_freq[idx];
}

freq_t mock_expected_tolerance(int idx)
{
    if (idx < 0 || idx >= profile_expect_count)
        return 0;
    return profile_expect_tol[idx];
}

freq_t mock_profile_min_freq(void)
{
    return profile_min_freq;
}

freq_t mock_profile_max_freq(void)
{
    return profile_max_freq;
}
