#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include "cachelab.h"

static unsigned s, E, b;//s为组索引位数 S = 2^s, E为行数, b为块偏移的位数
char *filename;
static unsigned totalSet;//高速缓存组的总数
static int hits, evictions, misses;

//运用LRU机制的缓存组用双向链表来实现，属于该组的缓存行用节点Node表示
typedef struct Node{
    struct Node *prev;
    struct Node *next;
    unsigned tag;//标识位
}Node; 

//LRU结构体记录一个高速缓存组的头尾节点和当前大小
typedef struct LRU{
    struct Node *head;
    struct Node *tail;
    unsigned size;
}LRU;

static LRU **lru; //lru是LRU结构体指针数组,数组索引为组索引

//init 初始化第i个高速缓存组
void init (int i){
    lru[i] = (LRU *)malloc(sizeof(LRU));
    lru[i]->head = (Node*) malloc(sizeof(Node));
    lru[i]->tail = (Node*) malloc(sizeof(Node));
    lru[i]->head->next = lru[i]->tail, lru[i]->head->prev = NULL;
    lru[i]->tail->prev = lru[i]->head, lru[i]->tail->next = NULL;
    lru[i]->size = 0;
}

//解析命令行，getopt返回参数的ASCII码；分析结束返回-1；若包含optstring中未定义参数，返回?字符
// 命令行形式为 ./csim-ref -s 4 -E 1 -b 4 -t traces/yi.trace
//opt接收getopt函数的返回值, optarg为字符串类型，表示参数的值
void parseOption(int argc, char *argv[]){
    int opt;
    char *optstring = "s:E:b:t:";
    while((opt = getopt(argc, argv, optstring))!= -1) {
        switch (opt)
        {
        case 's':
            s = atoi(optarg);//atoi 转换字符串为整数
            break;
        case 'E':
            E = atoi(optarg);
            break;
        case 'b':
            b = atoi(optarg);
            break;
        case 't':
            filename = optarg;
        }
    }
    totalSet = 1 << s;
}

//将被调用的缓存行断开当前连接
void deleteNode(Node *cache_line){
    cache_line->next->prev = cache_line->prev;
    cache_line->prev->next = cache_line->next;
}
//将被调用的缓存行放入双向链表头结点的右侧
void insert(LRU *cache_set, Node *cache_line){
    Node *head = cache_set->head;
    cache_line->next = head->next, cache_line->prev = head;
    head->next->prev = cache_line, head->next = cache_line;
}

void update(unsigned address){
    unsigned mask = 0xFFFFFFFF;
    unsigned setMask = mask >> (32 - s);
    unsigned targetSet = setMask & (address >> b);
    unsigned targetTag = address >> (b + s);

    LRU *curSet = lru[targetSet];
    Node *cur = curSet->head->next;
    Node *tail = curSet->tail;
    //开始遍历链表，如果找到匹配的缓存行，则将其放于链表前段
    int found = 0;
    while(cur != tail){
        unsigned tag = cur->tag;
        if(targetTag == tag){
            found = 1;
            break;
        }
        cur = cur->next;
    }

    if(found){
        hits++;
        deleteNode(cur);
        insert(curSet, cur);
        printf("命中高速缓存组 %d ！\n\n", targetSet);
    } else {
        misses++;
        Node *newNode = (Node*)malloc(sizeof(Node));
        newNode->tag = targetTag;
        if(curSet->size == E){
            Node *discard = tail->prev;
            deleteNode(discard);
            free(discard);
            insert(curSet, newNode);
            evictions++;
            printf("miss + evic set -> %d\n\n", targetSet);
        }else{
            curSet->size++;
            insert(curSet, newNode);
            printf("only miss set -> %d\n\n", targetSet);
        }
    }
}

void cacheSimulator(char *filename){
    int i;
    char op;
    unsigned address, size;

    lru = (LRU **)malloc(totalSet * sizeof(LRU *));//为LRU指针数组分配空间
    for(i = 0; i < totalSet; i++) init(i);

    FILE* file = fopen(filename, "r");
    while(fscanf(file, " %c %x,%d", &op, &address, &size) > 0){
        printf("%c %x %d\n", op, address, size);
        switch (op)
        {
        case 'L':
            update(address);
            break;
        case 'M': //修改M相当于 加载+存储,因此不需要break
            update(address);
        case 'S':
            update(address);
            break;
        }
    }
}
int main(int argc, char *argv[])
{
    //解析命令行，获取s, E, b的值以及文件路径filename
    parseOption(argc, argv);
    
    //启动缓存模拟器，进行内存访问
    cacheSimulator(filename);

    printSummary(hits, misses, evictions);
    return 0;
}
