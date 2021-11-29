/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE 4 /*Word and header/footer size (bytes) */
#define DSIZE 8 /*Double word size (bytes)*/
#define CHUNKSIZE (1 << 12) /*Extend heap by this amount (bytes)*/

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/*Read and write a word at address p*/
#define GET(p) *(unsigned int*)(p)
#define PUT(p, val) *(unsigned int*)(p) = (val)

/*Read the size and allocated fields from address p*/
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/*Given block ptr bp, compute address of its header and footer*/
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/*Given block ptr bp, compute address of next and previous blocks*/
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))
/*得到块p的前驱和后继的地址*/
#define GET_PREV(p) (*(unsigned int*)(p))
#define GET_NEXT(p) (*((unsigned int*)(p) + 1))
/*将空闲块p的前驱设置为prev，后继设置为next*/
#define SET_PREV(p, prev) (*(unsigned int*)(p) = (prev)) 
#define SET_NEXT(p, next) (*((unsigned int*)(p) + 1) = (next))
/*分离存储的等价类的数量, 需要为奇数*/
#define num_ptr 15
#define DEBUG 1

static void *extend_heap(size_t words);
static void *extend_heap(size_t words);
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);
static void *coalesce(void *bp);
static void place(void *bp, size_t size);
static void *find_fit(size_t asize);
static void *recoalesce(void *ptr, size_t asize);
static void *next_merge(void *next, void *bp, size_t asize);
static void *prev_merge(void *prev, void *bp, size_t asize);
int mm_init(void);
void *mm_malloc(size_t size);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t size);


static char* heap_listp;
static char* list_start_ptr;

static size_t get_index(size_t asize){
    // printf("-------get_index-------:\n");
    // printf("asize = %d\n", asize);

    size_t t = asize;
    size_t bit_16, bit_8, bit_4, bit_2, bit_1, res;
    bit_16 = !!(asize >> 16) << 4; asize >>= bit_16;
    bit_8 = !!(asize >> 8) << 3; asize >>= bit_8;
    bit_4 = !!(asize >> 4) << 2; asize >>= bit_4;
    bit_2 = !!(asize >> 2) << 1; asize >>= bit_2;
    bit_1 = !!(asize >> 1); asize >>= bit_1;

    res = bit_16 + bit_8 + bit_4 + bit_2 + bit_1 + asize;//最高非零bit位,从1开始
    if(t == (1 << (res - 1))) res--;
    if(res < 4) return 0;
    res -= 4;
    if(res >= num_ptr) 
        return num_ptr - 1;

    return res;
}
/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    // printf("---------mm_init--------\n");
    int i;
    if((heap_listp = mem_sbrk((num_ptr + 3) * WSIZE)) == (void *)-1)
        return -1;
    for(i = 0; i < num_ptr; i++)
        PUT(heap_listp + i * WSIZE, NULL);
    PUT(heap_listp + (num_ptr + 0) * WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + (num_ptr + 1) * WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + (num_ptr + 2) * WSIZE, PACK(0, 1));

    list_start_ptr = heap_listp;
    heap_listp += (num_ptr + 1) * WSIZE;

    // printf("list_start_ptr = %p, heap_listp = %p\n", list_start_ptr,heap_listp);
    if(extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

static void *extend_heap(size_t words){
    // printf("--------extend_heap--------\n");
    char *bp;
    size_t size;
    size = ((words * WSIZE + DSIZE + DSIZE - 1) / DSIZE) * DSIZE;
    if((long)(bp = mem_sbrk(size)) == -1 )
        return NULL;
    // printf("bp = %p, size = %d\n", bp, size);
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    SET_PREV(bp, NULL);
    SET_NEXT(bp, NULL);
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    return coalesce(bp);
}

/*传递的bp的前驱和后继节点地址初始化为0*/
static void *coalesce(void *bp){
    // printf("\n----------coalesce----------\n");
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    // printf("bp = %p\tPREV_BLKP(bp)=%p\tNEXT_BLKP(bp)=%p\n",
    //     bp,PREV_BLKP(bp),NEXT_BLKP(bp));
    // printf("prev_alloc=%d\tnext_alloc=%d\nsize = %d\n", prev_alloc, next_alloc, size);

    if(!prev_alloc && next_alloc){
        remove_free_block(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else if(prev_alloc && !next_alloc){
        remove_free_block(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));   //易错：这里注意bp的头部size已经修改
    }
    else if(!prev_alloc && !next_alloc){
        remove_free_block(NEXT_BLKP(bp));
        remove_free_block(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    insert_free_block(bp);
    return bp;
}

/*按照地址排序，将空闲块插入到对应的等价类链表中*/
static void insert_free_block(void *bp){
    // printf("--------insert_free_block--------\n");
    if(bp == NULL) return; 
    size_t size = GET_SIZE(HDRP(bp));
    size_t index = get_index(size);

    //得到指向对应等价类链表头的指针
    unsigned int *group_ptr = list_start_ptr + index * WSIZE;
    char *head_ptr = *group_ptr;
    // printf("bp=%p\tsize=%d\tindex=%d\n",bp,size,index);
    // printf("group_ptr=%p\t*group_ptr=%p\n", group_ptr,head_ptr);
    if(head_ptr == NULL){
        *group_ptr = (unsigned int)bp;
        SET_PREV(bp, NULL);
        SET_NEXT(bp, NULL);
        return;
    }

    char *cur, *prev_cur;
    for(prev_cur = NULL,cur = head_ptr; cur; prev_cur = cur,cur = GET_NEXT(cur))
        if((unsigned int)bp < (unsigned int)cur)
            break;

    if(cur == NULL){//bp排在链表尾部
        SET_PREV(bp, prev_cur);
        SET_NEXT(prev_cur, bp);
        SET_NEXT(bp, NULL);
    }
    else if(cur == head_ptr){//bp排在链表头部
        SET_PREV(bp, NULL);
        SET_NEXT(bp, cur);
        SET_PREV(cur, bp);
        *group_ptr = (unsigned int)bp;//更新头结点
    }
    else{
        SET_PREV(bp, prev_cur);
        SET_NEXT(bp, cur);
        SET_NEXT(prev_cur, bp);
        SET_PREV(cur, bp);
    }
}

static void remove_free_block(void *bp){
    // printf("\n-------remove_free_block-------\n");
    if(bp == NULL || GET_ALLOC(HDRP(bp))) return;
    void *prev = GET_PREV(bp);
    void *next = GET_NEXT(bp);
    size_t size = GET_SIZE(HDRP(bp));
    size_t index = get_index(size);
    unsigned int *group_ptr = list_start_ptr + index * WSIZE;

    // printf("bp = %p\tprev = %p\tnext = %p\nsize = %d\tindex = %d\tgroup_ptr = %p\n"
    //         , bp, prev, next, size, index, group_ptr);

    SET_NEXT(bp, NULL);
    SET_PREV(bp, NULL);
    if(prev == NULL && next == NULL){//bp位于链表头,且只有一个节点
        *group_ptr = 0;
    }
    else if(prev == NULL){//bp位于链表头，有多个节点
        SET_PREV(next, NULL);
        *group_ptr = next;
    }
    else if(next == NULL){//位于链表尾部
        SET_NEXT(prev, NULL);
    }
    else{//位于链表中间
        SET_NEXT(prev, next);
        SET_PREV(next, prev);
    }
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize, extendsize;
    void *bp;

    if(size == 0) 
        return NULL;
    if(size < DSIZE)
        asize = 2 * DSIZE;
    else
        asize = ((DSIZE + size + DSIZE - 1) / DSIZE) * DSIZE;

    // printf("\nmm_malloc: asize = %d\n", asize);

    if((bp = find_fit(asize)) != NULL){
        place(bp, asize);
        return bp;
    }
    // printf("malloc: no enough space\n");
    extendsize = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

static void *find_fit(size_t asize){
    size_t index = get_index(asize);
    size_t temp = index;
    unsigned int *group_ptr = list_start_ptr + index * WSIZE;
    
    // printf("--------find_fit--------\n");
    // printf("asize=%d\tindex=%d\tgroup_ptr=%p\n",asize,index,group_ptr);
    while(*group_ptr == 0 && index < num_ptr){
        index++;
        group_ptr += 1;
    }
    if(index == num_ptr) {
        // printf("no fit free block!!\n");
        return NULL;
    }
    
    if(index != temp)//说明只要返回头结点即可
    {
        return *group_ptr;
    }
    else if(index == temp)//需要遍历组内元素
    {
        void *bp = *group_ptr;
        for(bp = *group_ptr; bp; bp = GET_NEXT(bp)){
            size_t size = GET_SIZE(HDRP(bp));
            if(size >= asize)
                return bp;
        }
        //组内均小于此大小，想其它组请求
        do{
            group_ptr += 1;
            index ++;
        }while(*group_ptr == 0 && index < num_ptr);
        if(index == num_ptr) {
            // printf("no fit free block!!\n");
            return NULL;
        }
        return *group_ptr;
    }
}

/*place 需要将头结点更新，可选地分割空闲块，将剩下的空闲块放入对应链表中*/
static void place(void *bp, size_t asize){
    // printf("--------place--------\n");
    size_t index = get_index(asize);
    size_t blk_size = GET_SIZE(HDRP(bp));

    // printf("bp = %p\tasize=%d\tindex=%d\tblk_size=%d\n"
    //     ,bp,asize, index,blk_size);

    remove_free_block(bp);
    if((blk_size - asize) > 2*DSIZE){//分割空闲块
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void *next = NEXT_BLKP(bp);
        PUT(HDRP(next), PACK(blk_size-asize, 0));
        PUT(FTRP(next), PACK(blk_size-asize, 0));
        SET_NEXT(next, NULL);
        SET_PREV(next, NULL);
        // printf("there are more free space.\n");
        // printf("new block ptr:next = %p\n", next);
        coalesce(next);
    }
    else{
        // printf("just enough for demand.\n");
        PUT(HDRP(bp), PACK(blk_size, 1));
        PUT(FTRP(bp), PACK(blk_size, 1));
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    if(bp == NULL) return;
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    SET_PREV(bp, NULL);
    SET_NEXT(bp, NULL);
    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
/*mm_realloc优化版本1:oldsize小于asize时利用前后块可能的空间，如果不能达到要求再使用mm_malloc*/
void *mm_realloc(void *ptr, size_t size){
    // printf("--------mm_realloc--------\n");
    if(ptr == NULL)
        return mm_malloc(size);
    if(size == 0){
        mm_free(ptr);
        return NULL;
    }
    size_t asize = ((size + DSIZE + DSIZE - 1) / DSIZE) * DSIZE;
    size_t old_size = GET_SIZE(HDRP(ptr));
    //从前的块的大小再分配后还可以有剩下的空闲块可以利用
    // printf("ptr = %p\told_size = %d\tasize = %d\n", ptr, old_size, asize);
    if(old_size >= asize){
        if((old_size - asize) >= 2*DSIZE){
            // printf("old_size >> asize\n");
            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));
            void *next = NEXT_BLKP(ptr);
            PUT(HDRP(next), PACK(old_size - asize, 0));
            PUT(FTRP(next), PACK(old_size - asize, 0));
            SET_PREV(next, NULL);
            SET_NEXT(next, NULL);
            coalesce(next);
            return ptr;
        }
        //原来的块完全被利用，不需要新的空间，直接返回
        else if(old_size >= asize){
            return ptr;
        }
    }
    //需要先考虑前后有无直接利用的空闲块，如果没有则需要调用mm_malloc
    else if(old_size < asize){
        // printf("old_size < asize\n");
        old_size -= DSIZE;
        size_t copysize = ((old_size > size) ? size: old_size);
        void *bp = recoalesce(ptr, asize);
        if(bp == NULL){//需要调用malloc
            // printf("mm_realloc use mm_malloc\n");
            bp = mm_malloc(size);
            if(bp == NULL) return NULL;
            memcpy(bp, ptr, copysize);
            mm_free(ptr);
        }
        return bp;
    }
}
//合并空闲块，使得达到大小asize的要求，这个版本在recoalesce中就完成原有数据的复制
static void *recoalesce(void *bp, size_t asize){
    // printf("\n--------recoalesce--------\n");
    void *prev = PREV_BLKP(bp);
    void *next = NEXT_BLKP(bp);
    size_t prev_alloc = GET_ALLOC(HDRP(prev));
    size_t next_alloc = GET_ALLOC(HDRP(next));
    size_t prev_size = GET_SIZE(HDRP(prev));
    size_t next_size = GET_SIZE(HDRP(next));
    size_t bp_size = GET_SIZE(HDRP(bp));
    
    // printf("bp=%p\tprev=%p\tnext=%p\n", bp, prev, next);
    // printf("prev_alloc=%d\tnext_alloc=%d\n",prev_alloc, next_alloc);
    // printf("asize=%d\tbp_size=%d\tprev_size=%d\tnext_size=%d\n",
    //     asize,bp_size, prev_size, next_size);

    if(prev_alloc && next_alloc)//前后均不为空闲块
        return NULL;
    else if(!prev_alloc && next_alloc){//前为空闲块,如果可用需要先remove再处理
        void *res = prev_merge(prev, bp, asize);
        // printf("recoalesce return: res = %p\n", res);
        return res;
    }
    else if(prev_alloc && !next_alloc){
        void *res = next_merge(next, bp, asize);
        // printf("recoalesce return: res = %p\n", res);
        return res;
    }
    else if(!prev_alloc && !next_alloc){
        void *res;
        if(prev_size + next_size + bp_size < asize) 
            return NULL;
        else if(prev_size + bp_size >= asize){//能合并前方空闲块就尽可能合并
            res = prev_merge(prev, bp, asize);
            // printf("recoalesce return: res = %p\n", res);
        }
        else{//不能单独合并前方空闲块，则先合并前驱空闲块，再合并后继空闲块
            void *temp = prev_merge(prev, bp, prev_size + bp_size);
            res = next_merge(NEXT_BLKP(prev), temp, asize);
            // printf("recoalesce return: res = %p\n", res);
        }
        return res;
    }
}
//确定前为空闲块的时候，进行合并，并将原有内容复制到端点
static void *prev_merge(void *prev, void *bp , size_t asize){
    size_t prev_size = GET_SIZE(HDRP(prev));
    size_t bp_size = GET_SIZE(HDRP(bp));
    if(prev_size + bp_size < asize) return NULL;
    else if((prev_size + bp_size - asize) >= 2*DSIZE){//可以再进行分割
        size_t size = prev_size + bp_size - asize;
        remove_free_block(prev);
        memmove(prev, bp, bp_size - DSIZE);
        PUT(HDRP(prev), PACK(asize, 1));
        PUT(FTRP(prev), PACK(asize, 1));
        PUT(HDRP(NEXT_BLKP(prev)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(prev)), PACK(size, 0));
        coalesce(NEXT_BLKP(prev));
        return prev;
    }
    else{//全部使用
        remove_free_block(prev);
        memmove(prev, bp, bp_size - DSIZE);
        PUT(HDRP(prev), PACK(prev_size + bp_size, 1));
        PUT(FTRP(prev), PACK(prev_size + bp_size, 1));
        return prev;
    }
}
static void *next_merge(void *next, void *bp, size_t asize){
    // printf("--------next_merge--------\n");
    size_t next_size = GET_SIZE(HDRP(next));
    size_t bp_size = GET_SIZE(HDRP(bp));
    if(next_size + bp_size < asize) return NULL;
    else if((next_size + bp_size - asize) >= 2*DSIZE){ //分割空闲块
        size_t size = next_size + bp_size - asize;
        remove_free_block(next);
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        PUT(HDRP(NEXT_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        coalesce(NEXT_BLKP(bp));
        return bp;
    }
    else{//全部利用
        remove_free_block(next);
        PUT(HDRP(bp), PACK(next_size + bp_size, 1));
        PUT(FTRP(bp), PACK(next_size + bp_size, 1));
        return bp;
    }
}
/*感觉碎片会更少，因为总是将有效载荷的起点放在端点，但不知道为什么分数偏低
Results for mm malloc:
trace  valid  util     ops      secs  Kops
 0       yes   99%    5694  0.001441  3952
 1       yes  100%    5848  0.000773  7570
 2       yes   99%    6648  0.000991  6709
 3       yes  100%    5380  0.000778  6920
 4       yes   99%   14400  0.001171 12299
 5       yes   94%    4800  0.001297  3700
 6       yes   93%    4800  0.001299  3695
 7       yes   55%   12000  0.016010   750
 8       yes   51%   24000  0.052439   458
 9       yes   46%   14401  0.006272  2296
10       yes   45%   14401  0.001755  8204
Total          80%  112372  0.084226  1334

Perf index = 48 (util) + 40 (thru) = 88/100
*/