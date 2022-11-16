#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cachelab.h"

typedef struct InitOption {
    bool verbose;
    unsigned s, E, b;
    FILE* fp;
} InitOption;

typedef struct Summary {
    unsigned hits, misses, evictions;
} Summary;

typedef struct TraceLine {
    enum TraceType { L, S, M } type;
    unsigned long mem_addr;
} TraceLine, *TraceLineP;

typedef struct Cache {
    bool used;
    unsigned long tag;
    unsigned num;
} Cache, *CacheArray;

typedef enum CacheStatus { HIT, MISS, EVICTION } CacheStatus;

InitOption parse_args(int, char**);
Summary process_traceinfo(InitOption);

int main(int argc, char** argv) {
    InitOption op = parse_args(argc, argv);
    Summary sp = process_traceinfo(op);

    printSummary(sp.hits, sp.misses, sp.evictions);

    return 0;
}

InitOption parse_args(int argc, char** argv) {
    const char help_info[] =
        "Usage: csim [-hv] -s <num> -E <num> -b <num> -t <file>\n\
Options:\n\
  -h         Print this help message.\n\
  -v         Optional verbose flag.\n\
  -s <num>   Number of set index bits.\n\
  -E <num>   Number of lines per set.\n\
  -b <num>   Number of block offset bits.\n\
  -t <file>  Trace file.\n\
\n\
Examples:\n\
  linux>  csim -s 4 -E 1 -b 4 -t traces/yi.trace\n\
  linux>  csim -v -s 8 -E 2 -b 4 -t traces/yi.trace";
    bool verbose = false;
    int s = 0, E = 0, b = 0;
    FILE* fp = NULL;

    int ch;
    while ((ch = getopt(argc, argv, "s:E:b:t:v::h::")) != -1) {
        switch (ch) {
			case 'h':
				printf(help_info);
				exit(0);
            case 'v':
                verbose = true;
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 'E':
                E = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 't':
                if ((fp = fopen(optarg, "r")) == NULL) {
                    fprintf(stderr, "%s: No such file or directory\n", optarg);
                    exit(1);
                }
                break;
        }
    }

    if (s == 0 || E == 0 || b == 0 || fp == NULL) {
        fprintf(stderr, "%s: Missing required command line argument\n%s\n",
                argv[0], help_info);
        exit(1);
    }

    InitOption op;
    op.verbose = verbose;
    op.s = s;
    op.E = E;
    op.b = b;
    op.fp = fp;
    return op;
}

bool parse_traceinfo(char* buf, TraceLineP trace) {
    switch (buf[1]) {
        case 'L':
            trace->type = L;
            break;
        case 'S':
            trace->type = S;
            break;
        case 'M':
            trace->type = M;
            break;
        case ' ':
            return false;
        default:
            fprintf(stderr, "Trace file parse error\nline content: \"%s\"\n",
                    buf);
            return false;
    };
    if (sscanf(buf + 3, "%lx,%*d", &trace->mem_addr) != 1) {
        fprintf(stderr, "Trace file parse error\nline content: \"%s\"\n", buf);
        return false;
    }
    return true;
}

#define INDEX(i, j, E) ((i) * (E) + (j))

CacheStatus cache_produce(CacheArray cache, unsigned group, unsigned tag,
                          unsigned row, unsigned num) {
    for (int i = 0; i != row; i++) {
        if (cache[INDEX(group, i, row)].used &&
            cache[INDEX(group, i, row)].tag == tag) {
            cache[INDEX(group, i, row)].num = num;
            return HIT;
        }
    }

    for (int i = 0; i != row; i++) {
        if (!cache[INDEX(group, i, row)].used) {
            cache[INDEX(group, i, row)].used = true;
            cache[INDEX(group, i, row)].tag = tag;
            cache[INDEX(group, i, row)].num = num;
            return MISS;
        }
    }

    int earliest_i = 0, earliest_num = cache[INDEX(group, 0, row)].num;
    for (int i = 1; i != row; i++) {
        if (cache[INDEX(group, i, row)].num < earliest_num) {
            earliest_i = i;
            earliest_num = cache[INDEX(group, i, row)].num;
        }
    }
    cache[INDEX(group, earliest_i, row)].tag = tag;
    cache[INDEX(group, earliest_i, row)].num = num;
    return EVICTION;
}

#define GROUP(addr, b, s) (((addr) >> (b)) & ((1 << (s)) - 1))
#define TAG(addr, b, s) ((addr) >> ((b) + (s)))

Summary process_traceinfo(InitOption op) {
    Summary sum;
    sum.hits = 0;
    sum.misses = 0;
    sum.evictions = 0;

    char buf[100];
    assert(op.s < 48);
    unsigned group_size = 1 << op.s;
    CacheArray cache = malloc(sizeof(Cache) * group_size * op.E);
    TraceLine trace;
    unsigned num = 0;

    for (int i = 0; i != group_size; i++) {
        for (int j = 0; j != op.E; j++) {
            cache[INDEX(i, j, op.E)].used = false;
        }
    }

    while (fgets(buf, 100, op.fp) != NULL) {
        num++;
        CacheStatus status;
        if (!parse_traceinfo(buf, &trace)) continue;
        status = cache_produce(cache, GROUP(trace.mem_addr, op.b, op.s),
                               TAG(trace.mem_addr, op.b, op.s), op.E, num);
        buf[strlen(buf) - 1] = '\0';
        if (op.verbose) {
            printf("%s %s%s\n", buf + 1,
                   status == HIT ? "hit"
                                 : (status == MISS ? "miss" : "miss eviction"),
                   trace.type == M ? " hit" : "");
        }
        if (status == HIT) {
            sum.hits++;
        } else if (status == MISS) {
            sum.misses++;
        } else {
            sum.misses++;
            sum.evictions++;
        }
        if (trace.type == M) {
            sum.hits++;
        }
    }

    free(cache);
    return sum;
}
