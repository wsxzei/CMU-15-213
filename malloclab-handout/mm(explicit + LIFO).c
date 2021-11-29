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
/*Get and set prev or next pointer from address p*/
#define GET_PREV(p) (*(unsigned int*)(p))
#define SET_PREV(p, prev) (*(unsigned int*)(p) = (prev)) 
#define GET_NEXT(p) (*((unsigned int*)(p) + 1))
#define SET_NEXT(p, next) (*((unsigned int*)(p) + 1) = (next))

static char *heap_listp;
static char *free_list_head;

static void *extend_heap(size_t words);
static void remove_from_list(void *bp);
static void insert_into_list(void *bp);
static void *coalesce(void *bp);
static void place(void *bp, size_t size);
static void *find_fit(size_t size);
void *mm_malloc(size_t size);
int mm_init(void);
void *mm_realloc(void *ptr, size_t size);
void mm_free(void *ptr);


/*堆初始化时以及mm_malloc不能找到合适的匹配块时，使用extend_heap增加堆空间*/
static void *extend_heap(size_t words){
    char *bp;
    size_t size;
    /*Allocate an even number of words to maintain alignment*/
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if((long)(bp = mem_sbrk(size)) == -1) //bp现在位于初始的终止头后面的块
        return NULL;
    
    /*Initialize free block header/footer and the epilogue header*/
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    SET_PREV(bp, NULL);
    SET_NEXT(bp, NULL);
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));//size包括了有效载荷、头脚部、对齐填充
    
    /*如果之前的块时空闲的则合并，并返回空闲块的指针*/
    return coalesce(bp);
}
//将bp插入到空闲链表头部
static void insert_into_list(void *bp){
    if(bp == NULL) return;
    if(free_list_head == NULL){
        free_list_head = bp;
        return;
    }
    SET_NEXT(bp, free_list_head);
    SET_PREV(free_list_head, bp);
    SET_PREV(bp, NULL);
    free_list_head = bp;
    return;
}

static void remove_from_list(void *bp){
    if(bp == NULL)   return;
    void *prev = GET_PREV(bp);//指向bp前驱节点的指针
    void *next = GET_NEXT(bp);//指向bp后继节点的指针
    /*易错：coalesce合并的时候会将新的块放到链表头，其前驱为NULL;在insert前设置新的头结点的前驱为NULL也可以*/
    // SET_NEXT(bp, NULL);
    // SET_PREV(bp, NULL);
    if(prev == NULL && next == NULL){
        free_list_head = NULL;
//易错：这一步我忽略了，如果只有一个头结点却没有删除，在合并的时候会导致新头结点指针指向数据字块(即原头结点)
    }
    else if(prev == NULL){
        //说明bp为双向链表的的头结点
        free_list_head = next;
        SET_PREV(next, NULL);
    }
    else if(next == NULL){
        //bp为双向链表的尾节点
        SET_NEXT(prev, NULL);
    }
    else{
        SET_NEXT(prev, next);
        SET_PREV(next, prev);
    }

}

//注意传入bp时，其不为双向链表的节点
static void *coalesce(void *bp){
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    
    // if(prev_alloc && next_alloc) free的内存需要放入链表
      
    if(!prev_alloc && next_alloc){
        remove_from_list(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else if(prev_alloc && !next_alloc){
        remove_from_list(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));   //易错：这里注意bp的头部size已经修改
    }
    else if(!prev_alloc && !next_alloc){
        remove_from_list(NEXT_BLKP(bp));
        remove_from_list(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    insert_into_list(bp);
    return bp;
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);
    PUT(heap_listp + WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 2*WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 3*WSIZE, PACK(0, 1));
    heap_listp += 2 * WSIZE;
    free_list_head = NULL;

    if(extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

static void *find_fit(size_t size){
    void *cur;
    //之前写成了 GET_NEXT(cur) != NULL，细心！！！
    for(cur = free_list_head; cur != NULL; cur = GET_NEXT(cur)){
        if(GET_SIZE(HDRP(cur)) >= size)
            return cur;
    }
    return NULL;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    void *bp;

    if(size == 0) return NULL;
    if(size <= DSIZE) 
        asize = 4 * WSIZE;//最小为4个字，16字节
    else 
        asize = ((DSIZE + size + DSIZE - 1) / DSIZE) * DSIZE; //向上取为8的整数
    if((bp = find_fit(asize)) != NULL){
        place(bp, asize);
        return bp;
    }
    extendsize = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

static void place(void *bp, size_t asize){
    size_t csize = GET_SIZE(HDRP(bp));
    remove_from_list(bp);
    if((csize - asize) >= 2*DSIZE){
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void *next = NEXT_BLKP(bp);
        PUT(HDRP(next), PACK(csize - asize, 0));
        PUT(FTRP(next), PACK(csize - asize, 0));
        SET_NEXT(next, NULL);
        SET_PREV(next, NULL);
        coalesce(next);//注意：出现新的空闲块，都调用coalesce合并空闲块，也更新了双向链表
    }
    else{
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    if(ptr == NULL) return;
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    SET_PREV(ptr, NULL);
    SET_NEXT(ptr, NULL);
    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize, prevSize;

    newptr = mm_malloc(size);
    if(newptr == NULL) return NULL;
    prevSize = GET_SIZE(HDRP(oldptr));
    copySize = ((prevSize > size) ? size : prevSize);
    memcpy(newptr, oldptr, copySize - WSIZE);
    mm_free(oldptr);
    return newptr;
}
/*
Results for mm malloc:
trace  valid  util     ops      secs  Kops
 0       yes   89%    5694  0.000534 10665
 1       yes   92%    5848  0.000444 13165
 2       yes   94%    6648  0.000803  8277
 3       yes   96%    5380  0.000607  8857
 4       yes   66%   14400  0.000605 23817
 5       yes   88%    4800  0.000942  5096
 6       yes   85%    4800  0.002042  2351
 7       yes   55%   12000  0.005784  2075
 8       yes   51%   24000  0.004088  5871
 9       yes   26%   14401  0.074187   194
10       yes   34%   14401  0.003536  4073
Total          71%  112372  0.093571  1201

Perf index = 42 (util) + 40 (thru) = 82/100
*/