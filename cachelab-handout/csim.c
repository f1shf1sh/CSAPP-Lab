#include "cachelab.h"
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>

typedef uint8_t U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;
typedef int STATUS;

typedef struct CacheLine {
    U64 sign;
    U64 tag;
    U8 *block;
} CacheLine;

typedef struct CacheInfo {
    U64 sets;
    U64 lines;
    U64 blockSize;
}CacheInfo;

typedef struct CacheLinkList {
    CacheLine *cacheLine;
    struct CacheLinkList *prev;
    struct CacheLinkList *next;
}CacheLinkList;

typedef struct Cache {
    CacheLinkList **linkListArr;
    CacheInfo *cacheInfo;
    U64 *cacheLineCount;
}Cache;

enum Status {
    MISS, HIT, EVICTION
};

typedef struct StatsHit {
    STATUS hit;
    STATUS miss;
    STATUS  evicts;
} StatsHit;

typedef struct Parm {
    U64 sets;
    U64 lines;
    U64 blocks;
    U64 s;
    U64 b;
    U64 E;
    U8 verbosity;
} Parm;

typedef struct AddInfo {
    //address mark struct
    U64 t;
    U64 s;
    U64 b;
} AddInfo;

typedef struct Address {
    char op;
    U64 add;
    U64 size;
    AddInfo *addInfo;
} Address;

typedef struct AddLinkList {
    Address *address;
    struct AddLinkList *prev;
    struct AddLinkList *next;
}AddLinkList;

Cache* initCache(U64 sets, U64 lines, U64 blocks) {
    Cache* cache = (Cache*)malloc(sizeof(Cache));
    CacheInfo *cacheInfo = (CacheInfo*)malloc(sizeof(CacheInfo));
    CacheLinkList** linkListArr = (CacheLinkList**)malloc(sizeof(CacheLinkList*)*sets);
    U64 *cacheLineCount = (U64*) malloc(sizeof(U64)*sets);

    memset(cache, 0, sizeof(Cache));
    memset(cacheInfo, 0, sizeof(CacheInfo));
    memset(linkListArr, 0, sizeof(CacheLinkList*)*sets);
    memset(cacheLineCount, 0, sizeof(U64)*sets);

    cache->cacheInfo = cacheInfo;
    cache->cacheLineCount = cacheLineCount;
    cache->linkListArr = linkListArr;
    cache->cacheInfo->sets = sets;
    cache->cacheInfo->lines = lines;
    cache->cacheInfo->blockSize = blocks;

    return cache;
}

void freeCache(Cache *cache) {
    U64 sets = cache->cacheInfo->sets;
    CacheLinkList *headNode = 0, *node = 0, *delNode = 0;
    int i;
    for (i = 0; i < sets; ++i) {
        headNode = cache->linkListArr[i];
        node = headNode;
        while(node != 0) {
            delNode = node;
            node = node->next;
            free(delNode->cacheLine);
            free(delNode);
        }
    }
    free(cache->linkListArr);
    free(cache->cacheLineCount);
    free(cache->cacheInfo);
    free(cache);
}

STATUS seekCache(Cache *cache, Address *add) {
    U64 tag = add->addInfo->t;
    U64 set = add->addInfo->s;
    U64 lines = cache->cacheInfo->lines;
    U64 lineCount = cache->cacheLineCount[set];
    CacheLinkList *head = cache->linkListArr[set];

    // first match if success return hit
    if (head == 0) {
        head = (CacheLinkList*) malloc(sizeof(CacheLinkList));
        cache->linkListArr[set] = head;
        CacheLine* cacheLine = (CacheLine*)malloc(sizeof(CacheLine));
        head->cacheLine = cacheLine;
        head->next = 0;
        head->prev = 0;
        head->cacheLine->sign = 1;
        head->cacheLine->tag = tag;
        head->cacheLine->block = 0;
        cache->cacheLineCount[set]++;
        return MISS;
    }

    CacheLinkList *node = head;
    CacheLinkList *lastNode = 0;
    CacheLinkList *newNode = 0;
    while(node != 0) {
        if(node->cacheLine->tag == tag) {
            if (node->prev == 0) {
                // first node
                cache->linkListArr[set] = node;
            }
            else if (node->next == 0) {
                // last node
                lastNode = node->prev;
                lastNode->next = 0;
                head->prev = node;
                node->next = head;
                node->prev = 0;
                cache->linkListArr[set] = node;
            }
            else {
                node->prev->next = node->next;
                node->next->prev = node->prev;
                head->prev = node;
                node->next = head;
                node->prev = 0;
                cache->linkListArr[set] = node;
            }
            return HIT;
        }
        lastNode = node;
        node = node->next;
    }

    if (lineCount < lines) {
        // add to link list last
        newNode = (CacheLinkList*)malloc(sizeof(CacheLinkList));
        CacheLine* cacheLine = (CacheLine*)malloc(sizeof(CacheLine));
        newNode->cacheLine = cacheLine;
        newNode->cacheLine->sign = 1;
        newNode->cacheLine->tag = tag;

        newNode->next = head;
        newNode->prev = 0;
        head->prev = newNode;
        cache->linkListArr[set] = newNode;
        cache->cacheLineCount[set]++;
        return MISS;
    }

    newNode = (CacheLinkList*) malloc(sizeof(CacheLinkList));
    CacheLine* cacheLine = (CacheLine*)malloc(sizeof(CacheLine));
    memset(newNode, 0, sizeof (CacheLinkList));
    memset(cacheLine, 0, sizeof(CacheLine));
    newNode->cacheLine = cacheLine;
    newNode->cacheLine->tag = tag;

    if (lastNode->prev == 0) {
        cache->linkListArr[set] = newNode;
        free(lastNode->cacheLine);
        free(lastNode);
    }
    else {
        lastNode = lastNode->prev;
        free(lastNode->next->cacheLine);
        free(lastNode->next);
        lastNode->next = 0;
        newNode->next = head;
        newNode->prev = 0;
        head->prev = newNode;
        cache->linkListArr[set] = newNode;
    }
    return EVICTION;
}

void paresAddress(U64 add, U32 s, U32 b, Address *address) {
    U32 S = pow(2, s) - 1;
    U32 B = pow(2, b) - 1;
    address->addInfo->b = add & B;
    add >>= b;
    address->addInfo->s = add & S;
    add >>= s;
    address->addInfo->t = add;
}

AddLinkList* initTraceFile(const char* fileName, U32 s, U32 b) {
    char op;
    U64 add;
    U32 size;
    FILE *file;
    AddLinkList *head = 0, *node = 0, *tail = 0;
    Address *address;
    AddInfo *addInfo;
    file=fopen(fileName,"r");
    while(fscanf(file," %c %lx,%d",&op,&add,&size)==3){
        node = (AddLinkList*)malloc(sizeof(AddLinkList));
        address = (Address*)malloc(sizeof(Address));
        addInfo = (AddInfo*)malloc(sizeof(AddInfo));
        memset(node, 0, sizeof(AddLinkList));
        memset(address, 0, sizeof(Address));
        memset(addInfo, 0, sizeof(AddInfo));

        node->address = address;
        node->address->addInfo = addInfo;
        node->address->op = op;
        node->address->add = add;
        node->address->size = size;
        paresAddress(add, s, b, address);
        if (head == 0) {
            head = node;
            node->next = 0;
            tail = head;
        } else {
            tail->next = node;
            tail = node;
            tail->next = 0;
        }
    }
    fclose(file);
    return head;
}

void freeTraceFile(AddLinkList *headNode) {
    AddLinkList  *node = headNode, *delNode = 0;
    while(node != 0) {
        delNode = node;
        node = node->next;
        free(delNode->address->addInfo);
        free(delNode->address);
        free(delNode);
    }
}

void printUsage()
{
    printf("Usage: ./csim-ref [-hv] -s <num> -E <num> -b <num> -t <file>\n"
           "Options:\n"
           "  -h         Print this help message.\n"
           "  -v         Optional verbose flag.\n"
           "  -s <num>   Number of set index bits.\n"
           "  -E <num>   Number of lines per set.\n"
           "  -b <num>   Number of block offset bits.\n"
           "  -t <file>  Trace file.\n\n"
           "Examples:\n"
           "  linux>  ./csim-ref -s 4 -E 1 -b 4 -t traces/yi.trace\n"
           "  linux>  ./csim-ref -v -s 8 -E 2 -b 4 -t traces/yi.trace\n");
}
void chStatus(StatsHit *pStatsHit, STATUS status) {
    switch (status) {
        case HIT:
            pStatsHit->hit++;
            break;
        case MISS:
            pStatsHit->miss++;
            break;
        case EVICTION:
            pStatsHit->miss++;
            pStatsHit->evicts++;
            break;
    }
}

void simulates(Cache* cache, AddLinkList *headNode, StatsHit *pStatsHit) {
    char op;
    STATUS status;
    AddLinkList *node = headNode;
    while (node != 0) {
        op = node->address->op;
        switch (op) {
            case 'L':
                status = seekCache(cache, node->address);
                chStatus(pStatsHit, status);
                break;
            case 'S':
                status = seekCache(cache, node->address);
                chStatus(pStatsHit, status);
                break;
            case 'M':
                status = seekCache(cache, node->address);
                chStatus(pStatsHit, status);
                status = seekCache(cache, node->address);
                chStatus(pStatsHit, status);
                break;
            default:
                break;
        }
        node = node->next;
    }
}

int main(int argc, char* argv[]) {
    int ch;
    const char *traceFile;
    StatsHit statsHit = {0, 0, 0};
    AddLinkList *headNode = 0;
    Cache *cache = 0;
    Parm parm = {0,0,0,0,0,0,0};
    while ((ch = getopt(argc, argv, "s:E:b:t:vh")) != -1) {
        switch (ch) {
            case 's':
                parm.s = atoi(optarg);
                parm.sets = pow(2, parm.s);
                break;
            case 'E':
                parm.E = atoi(optarg);
                parm.lines = parm.E;
                break;
            case 'b':
                parm.b = atoi(optarg);
                parm.blocks = pow(2, parm.b);
                break;
            case 't':
                traceFile = optarg;
                break;
            case 'v':
                parm.verbosity = 1;
                break;
                // print parameter prototype
            case 'h':
                printUsage();
                exit(0);
            default:
                printUsage();
        }
    }

    headNode = initTraceFile(traceFile, parm.s, parm.b);
    cache = initCache(parm.sets, parm.lines, parm.blocks);
    simulates(cache, headNode, &statsHit);
    printSummary(statsHit.hit, statsHit.miss, statsHit.evicts);
    freeCache(cache);
    freeTraceFile(headNode);

    return 0;
}
