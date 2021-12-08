#include "csapp.h"
#include "cache.h"

/*初始化缓存系统*/
cache_t* cache_init(void){
    cache_t *cache = (cache_t *)Malloc(sizeof(cache_t));
    cache->head = (block_t *)Malloc(sizeof(block_t));
    cache->tail = (block_t *)Malloc(sizeof(block_t));
    cache->head->prev = NULL, cache->head->next = cache->tail;
    cache->tail->prev = cache->head, cache->tail->next = NULL;
    pthread_rwlock_init(&cache->rwlock, NULL);
    cache->total_size = 0;
    printf("cache=%p\thead=%p\ttail=%p\n", cache, cache->head, cache->tail);
    printf("head_next=%p\ttail_prev=%p\n",cache->head->next, cache->tail->prev);
    return cache;
}

/*找到指定指定url的缓存块节点,找到则返回block_t的指针,否则返回NULL*/
block_t *find_block(cache_t *cache , char *url){
    pthread_rwlock_rdlock(&cache->rwlock);
    printf("--------find_block---------\ncache=%p\turl=%s\n", cache, url);
    block_t *cur = cache->head->next;
    block_t *tail = cache->tail;

    printf("cur = %p\ttail = %p\n", cache->head->next, cache->tail);

    for(; cur != tail; cur = cur->next){
        printf("cur=%p\tcur_url=%s\n", cur, cur->url);

        if(!strcmp(url, cur->url)){ //url匹配
            pthread_rwlock_unlock(&cache->rwlock);
            printf("result url=%s\n", cur->url);
            return cur;
        }
    }
    pthread_rwlock_unlock(&cache->rwlock);
    return NULL;
}

block_t *build_block(char*_url, char*_content, int _size){
    printf("---------build _block--------\n");
    block_t* block;
    block = (block_t *)Malloc(sizeof(block_t));
    // block->url = _url;//这步有问题！
    block->url = (char *)Malloc(strlen(_url) + 10);
    strcpy(block->url, _url);
    printf("_content=%p   _size=%d\n", _content, _size);
    block->content = (char *)Realloc(_content, _size);
    block->size = _size;
    pthread_rwlock_init(&block->rwlock, NULL);
    printf("---------end_build--------\n");
    return block;
}

/*将缓存块插入到头结点的next,需要更改totalsize*/
void insert_block(cache_t *cache, block_t *block){
    printf("--------insert_block--------\n");
    printf("cache=%p\tblock=%p\n", cache, block);
    pthread_rwlock_wrlock(&cache->rwlock);
    pthread_rwlock_wrlock(&block->rwlock);
    block_t *head = cache->head;
    head->next->prev = block;
    block->next = head->next;
    block->prev = head;
    head->next = block;
    cache->total_size += block->size;
    pthread_rwlock_unlock(&block->rwlock);
    pthread_rwlock_unlock(&cache->rwlock);
    printf("--------end_insert--------\n");
}

/*将指定缓存块移除,并返回其指针*/
void remove_block(cache_t *cache, block_t *block){
    pthread_rwlock_wrlock(&cache->rwlock);
    pthread_rwlock_wrlock(&block->rwlock);
    block->next->prev = block->prev;
    block->prev->next = block->next;
    block->prev = block->next = NULL;
    (cache->total_size) -= (block->size);
    pthread_rwlock_unlock(&block->rwlock);
    pthread_rwlock_unlock(&cache->rwlock);
}

void free_block(block_t *block){
    pthread_rwlock_wrlock(&block->rwlock);
    //释放block_t节点，还需释放content
    free((void *)(block->url));
    free((void *)(block->content));
    free((void *)block);
}

int get_total_size(cache_t *cache){
    pthread_rwlock_rdlock(&cache->rwlock);
    int cnt = cache->total_size;
    pthread_rwlock_unlock(&cache->rwlock);
    return cnt;
}

char *get_content(cache_t *cache, block_t* block){
    pthread_rwlock_rdlock(&cache->rwlock);
    pthread_rwlock_rdlock(&block->rwlock);
    char *result = (char *)Malloc(block->size);
    memcpy(result, block->content, block->size);
    pthread_rwlock_unlock(&block->rwlock);
    pthread_rwlock_unlock(&cache->rwlock);
    return result;
}

int get_size(cache_t *cache, block_t *block){
    pthread_rwlock_rdlock(&cache->rwlock);
    pthread_rwlock_rdlock(&block->rwlock);
    int size = block->size;
    pthread_rwlock_unlock(&block->rwlock);
    pthread_rwlock_unlock(&cache->rwlock);
    return size;
}