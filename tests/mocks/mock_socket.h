#ifndef _MOCK_SOCKET_H_
#define _MOCK_SOCKET_H_

#include <stdbool.h>
#include "../../gqrx-prot.h"

#define MOCK_SOCKFD 42

/* Generic response-queue (used by protocol-unit tests) */
void mock_socket_reset(void);
void mock_socket_set_response(const char *response);
void mock_socket_add_response(const char *response);
const char* mock_socket_get_last_command(void);
const char* mock_socket_get_actual_host(void);
int mock_socket_get_actual_port(void);

/* Sweep-profile API */
bool mock_load_profile(const char *filename);
int  mock_expected_count(void);
freq_t mock_expected_freq(int idx);
freq_t mock_expected_tolerance(int idx);
freq_t mock_profile_min_freq(void);
freq_t mock_profile_max_freq(void);

/* Keypress queue (sweep tests) */
void mock_add_keypress(char c);
void mock_clear_keypresses(void);

#endif /* _MOCK_SOCKET_H_ */
