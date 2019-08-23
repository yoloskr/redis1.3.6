#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "config.h"
//判断操作系统
#if defined(__sun)
#define PREFIX_SIZE sizeof(long long)
#else
#define PREFIX_SIZE sizeof(size_t)

//内存使用总量
static size_t used_memory = 0;
//是否开启线程安全
static int zmalloc_thread_safe = 0;
//初始化互斥锁
ptread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;

//分配内存时增加内存使用的总量
#define increment_used_memory(_n) do {
      //是否开启线程安全
	  if(zmalloc_thread_safe) {
	  	//互斥锁上锁(线程没有抢到锁，挂起)
	  	pthread_mutex_lock(&used_memory_mutex);
	  	used_memory += _n;
	  	//互斥锁解锁
	  	pthread_mutex_unlock(&used_memory_mutex);
	  }else {
         used_memory += _n;
	  }
} while(0)

//释放内存时减少内存使用的总量
#define decrement_used_memory(_n) do {
       //是否开启线程安全
	  if(zmalloc_thread_safe) {
	  	 //互斥锁上锁
	  	 pthread_mutex_lock(&used_memory_mutex);
	  	 used_memory -= _n;
	  	 //互斥锁解锁
	  	 pthread_mutex_unlock(&used_memory_mutex);
	  }else {
	   	 used_memory -= _n;
	  }
} while(0)

//内存分配溢出
static void zmalloc_oom(size_t size) {
	fprintf(stderr, "zmalloc: Out of memory trying to allocate %zu bytes\n", size);
    //强制刷新输出缓冲区(标准错误stderr本身是无缓冲的，但是如果标准错误被重定向了（比如重定向到文件，文件默认是全缓冲的），有可能就是有缓冲的了。)
	fflush(stderr);
	//终止进程
	abort();
}

/**
*  1.根据操作系统的不同，分配内存策略不同
*  2.非apple system 每次分配多分配PREFIX_SIZE的长度，PREFIX_SIZE存放分配size大小，用于统计内存使用量
    apple system 自带方法可以通过分配的地址获取内存大小,所以不需要多分配PREFIX_SIZE大小的内存来保存每次分配的内存大小
*  3.返回实际存数据的开始地址，如下面会返回10009内存地址
*  10000[---prefix_size存放data的字节大小--]10009[-------data-------]
*/
void *zmalloc(size_t size) {
	void *ptr = malloc(size+PREFIX_SIZE);
	if (!ptr) zmalloc_oom(size);
//apple system	
#ifdef HAVE_MALLOC_SIZE
    increment_used_memory(redis_malloc_size(ptr));
     return ptr;
#else
    *( (size_t*) ptr) = size;
    //增加内存使用计数
    increment_used_memory(size+PREFIX_SIZE);
    //返回分配的内存中第PREFIX_SIZE处的地址
    return (char*)ptr+PREFIX_SIZE;
#endif    
}

/**重新分配内存
* 1.根据操作系统的不同，重新分配内存方式不同
* 2.非apple system，得到之前分配包含PREFIX_SIZE内存起始地址,即下面的10000，重新分配内存，原始的内存会被自动释放
    维护内存使用量，删除之前内存的使用量，增加新分配的内存使用量
* 3.apple system 自带可以通过内存地址获取内存大小的方法，其他逻辑和上面一样
* 10000[---prefix_size存放data的字节大小--]10009[-------data-------]
*/
void *zrealloc(void *ptr, size_t size) {
#ifndef HAVE_MALLOC_SIZE
	 void *realptr
#endif
	 size_t oldsize;
	 void *newptr;
	 if(ptr == NULL) return zmalloc_oom(size);
#ifdef HAVE_MALLOC_SIZE
	 //apple system通过地址获取内存大小
     oldsize = redis_malloc_size(ptr);
     newptr = realloc(ptr,size);
     if(newptr == NULL) return zmalloc_oom(size);
     decrement_used_memory(oldsize);
     increment_used_memory(redis_malloc_size(newptr));
     return newptr;
#else
   //真正分配的起始地址  
   realptr = (char *) ptr - PREFIX_SIZE;
   //数据占用的内存大小
   oldsize = *((size_t *) realptr);
   //重新分配内存
   // 如果分配成功, realptr会被自动释放, 所以这里不用再释放realptr
   newptr = realloc(realptr,size+PREFIX_SIZE);
   //分配失败处理
   if(newptr == NULL) return zmalloc_oom(size);
   //更新统计的内存大小
   decrement_used_memory(oldsize);
   //因为减少的时候没有减去PREFIX_SIZE,所以增加的时候只需要size
   increment_used_memory(size);
   return (char *)newptr + PREFIX_SIZE;
}

/**
*释放内存
*free只能从malloc（或alloc或realloc）申请的内存起始位置释放申请到的整块内存，不能部分释放
*/
void zfree(void *ptr) {
#ifndef HAVE_MALLOC_SIZE
    void *realptr;
    size_t oldsize;
#endif

    if (ptr == NULL) return;
#ifdef HAVE_MALLOC_SIZE
    decrement_used_memory(redis_malloc_size(ptr));
    free(ptr);
#else
    //获取分配内存的起始地址
    realptr = (char*)ptr-PREFIX_SIZE;
    //获取数据占用内存大小
    oldsize = *((size_t*)realptr);
    //维护数据使用计数
    decrement_used_memory(oldsize+PREFIX_SIZE);
    //释放内存
    free(realptr);
#endif
}

//获取使用的内存计数
size_t zmalloc_used_memory(void) {
    size_t um;
    if (zmalloc_thread_safe) pthread_mutex_lock(&used_memory_mutex);
    um = used_memory;
    if (zmalloc_thread_safe) pthread_mutex_unlock(&used_memory_mutex);
    return um;
}

//设置线程安全模式
void zmalloc_enable_thread_safeness(void) {
    zmalloc_thread_safe = 1;
}














