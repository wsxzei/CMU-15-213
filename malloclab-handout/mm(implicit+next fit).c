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

static char *heap_listp, *pre_listp;

static void *coalesce(void *bp);
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void place(void *, size_t );
static void *next_fit(size_t asize);

/*合并bp前后的空闲块，并返回合并后的空闲块的指针*/
static void *coalesce(void *bp){
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if(prev_alloc && next_alloc)//前后都为分配块
        return bp;
    else if(prev_alloc && !next_alloc){//前为分配块，后为空闲块
        if(bp == pre_listp || NEXT_BLKP(bp) == pre_listp)
            pre_listp = bp;
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));//因为头部size已经更改，因此FTRP已经是合并后空闲块的脚部
        
    }
    else if(!prev_alloc && next_alloc){//前为空闲块，后为分配块
        if(bp == pre_listp || PREV_BLKP(bp) == pre_listp)
            pre_listp = PREV_BLKP(bp);
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else if(!prev_alloc && !next_alloc){//前后都为空闲块
        if(bp == pre_listp || PREV_BLKP(bp) == pre_listp || NEXT_BLKP(bp) == pre_listp)
            pre_listp = PREV_BLKP(bp);
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))) + GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;
}
/*堆初始化时以及mm_malloc不能找到合适的匹配块时，使用extend_heap增加堆空间*/
static void *extend_heap(size_t words){
    char *bp;
    size_t size;
    /*Allocate an even number of words to maintain alignment*/
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if((long)(bp = mem_sbrk(size)) == -1){ //bp现在位于初始的终止头后面的块
        return NULL;
    }
    /*Initialize free block header/footer and the epilogue header*/
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));//size包括了有效载荷、头脚部、对齐填充
    
    /*如果之前的块时空闲的则合并，并返回空闲块的指针*/
    return coalesce(bp);
}

/*place将请求块放置在空闲块的起始位置，只有当剩余部分的大小等于或者超出最小块(16 bytes)的大小时才进行分割*/
static void place(void *bp, size_t asize){
    size_t csize = GET_SIZE(HDRP(bp));

    if((csize - asize) >= 2*DSIZE){
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
    }
    else{
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/* 
 * mm_init - initialize the malloc package.
 */
/*初始化堆*/
int mm_init(void){
    /*Create the initial empty heap*/
    if((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);//地址为heap_listp的四字节的块初始为0，作为双字对齐的填充块
    PUT(heap_listp + WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 2*WSIZE, PACK(DSIZE, 1));//初始化序言块为8字节已分配块，由一个头和一个脚组成
    PUT(heap_listp + 4*WSIZE, PACK(0, 1));//初始化的终止头

    heap_listp += 2*WSIZE;
    pre_listp = heap_listp;
    /*Extend the empty heap with a free block of CHUNKSIZE bytes*/
    if(extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
    
}

/*next fit:每次搜索从上一次查询结束的地方开始*/
static void* next_fit(size_t asize)
{
    for (char* bp = pre_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))
    {
        if (!GET_ALLOC(HDRP(bp)) && GET_SIZE(HDRP(bp)) >= asize)
        {
            pre_listp = bp;
            return bp;
        }
    }

    for (char* bp = heap_listp; bp != pre_listp; bp = NEXT_BLKP(bp))
    {
        if (!GET_ALLOC(HDRP(bp)) && GET_SIZE(HDRP(bp)) >= asize)
        {
            pre_listp = bp;
            return bp;
        }
    }
    return NULL;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
/*分配器调整块的大小，为头部和脚部留空间，并满足双子对齐的要求*/
void *mm_malloc(size_t size){
    size_t asize; /*Adjust block size*/
    size_t extendsize; /*Amount to extend heap if not fit*/
    char *bp;
    
    if(size == 0)
        return NULL;
    if(size < DSIZE)
        asize = 2 * DSIZE;
    else 
        asize = DSIZE * ((size + DSIZE + DSIZE - 1) / DSIZE);//向上舍入8的倍数

    /*search the free list for a fit*/
    if((bp = next_fit(asize)) != NULL){
        place(bp, asize);
        return bp;
    }

    /*No fit found. Get more memory and place the block*/
    extendsize = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp){
    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
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
    copySize = (prevSize > size) ? size : prevSize;
    memcpy(newptr, oldptr, copySize - WSIZE);
    mm_free(oldptr);
    return newptr;
}
/*
implicit + next fit
Results for mm malloc:
trace  valid  util     ops      secs  Kops
 0       yes   91%    5694  0.003294  1729
 1       yes   92%    5848  0.002089  2800
 2       yes   95%    6648  0.006162  1079
 3       yes   97%    5380  0.006346   848
 4       yes   66%   14400  0.000406 35494
 5       yes   91%    4800  0.007237   663
 6       yes   89%    4800  0.006626   724
 7       yes   55%   12000  0.013838   867
 8       yes   51%   24000  0.013796  1740
 9       yes   27%   14401  0.081297   177
10       yes   45%   14401  0.003320  4337
Total          73%  112372  0.144410   778

Perf index = 44 (util) + 40 (thru) = 84/100
*/