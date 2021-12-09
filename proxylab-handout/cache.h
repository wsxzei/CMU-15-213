#include "csapp.h"
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

//缓存块结构体,为双向链表的节点
typedef struct block_t{
    char *url; /*统一资源定位符*/
    char *content; /*buf指向缓存的数据*/
    struct block_t *prev, *next; /*节点的前驱和后继*/
    pthread_rwlock_t rwlock; 
    int size;
}block_t;

/**缓存系统结构体，记录头结点，尾部结点和整个缓存大小
 * 注意遍历链表的时候需要加读锁，修改链表结构的时候加写锁
*/
typedef struct cache_t{
    block_t *head, *tail;
    pthread_rwlock_t rwlock;
    int total_size;
}cache_t;

cache_t* cache_init(void);
block_t *find_block(cache_t *cache , char *url);
block_t *build_block(char*_url, char*_content, int _size);
void insert_block(cache_t *cache, block_t *block);
// void remove_block(cache_t *cache, block_t *block);
void free_block(block_t *block);
int get_total_size(cache_t *cache);
char *get_content(block_t* block);
int get_size(block_t *block);
void not_blocked_remove(cache_t *cache, block_t *block);
void not_blocked_insert(cache_t *cache, block_t *block);