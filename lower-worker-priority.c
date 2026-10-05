#define _GNU_SOURCE
#include <pthread.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
struct Start { void *(*fn)(void *); void *arg; };
static void *worker(void *raw) {
    struct Start start=*(struct Start*)raw; free(raw);
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 19);
    return start.fn(start.arg);
}
int pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*fn)(void*),void *arg) {
    static int (*real_create)(pthread_t*,const pthread_attr_t*,void*(*)(void*),void*);
    if(!real_create) real_create=dlsym(RTLD_NEXT,"pthread_create");
    struct Start *start=malloc(sizeof(*start));start->fn=fn;start->arg=arg;
    int result=real_create(thread,attr,worker,start);if(result)free(start);return result;
}
