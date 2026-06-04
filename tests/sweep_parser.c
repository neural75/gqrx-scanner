#include "sweep_parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int parse_hit_report(const char *captured, freq_t *hits, int max_hits)
{
    int count = 0;
    const char *p = captured;

    while (p && *p && count < max_hits)
    {
        /* Look for "Freq: " followed by the print_freq "XXX.XXX MHz" format */
        const char *tag = strstr(p, "Freq: ");
        if (!tag)
            break;

        tag += 6; /* skip "Freq: " */

        /* parse the integer MHz part */
        char *end = NULL;
        long mhz = strtol(tag, &end, 10);
        if (end == tag || *end != '.')
        {
            p = tag;
            continue;
        }

        /* skip '.' and parse the KHz fractional part */
        end++;
        long khz = strtol(end, &end, 10);

        hits[count++] = (freq_t)mhz * 1000000ULL + (freq_t)khz * 1000ULL;

        /* find the next newline to continue scanning */
        p = strchr(tag, '\n');
        if (!p)
            break;
    }

    return count;
}
