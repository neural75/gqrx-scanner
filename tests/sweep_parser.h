#ifndef SWEEP_PARSER_H
#define SWEEP_PARSER_H

#include "../gqrx-prot.h"

#define SWEEP_PARSER_MAX_HITS 256

int parse_hit_report(const char *captured, freq_t *hits, int max_hits);

#endif
