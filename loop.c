/*
 * loop.c
 *
 *  Created on: 2015-3-27
 *      Author: Administrator
 */


#include <linux/stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/timeb.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <sys/syscall.h> 
#include <string.h>
#include "loop.h"

static volatile int loop_default_size = 256;

#define LOOP(p) ((loop_t*)p)
#define LOCK(p) pthread_mutex_lock(&LOOP(p)->mutex)
#define UNLOCK(p) pthread_mutex_unlock(&LOOP(p)->mutex)


#define LOOP_FOREVER (-1)

#define LOOP_NULL   (-20)
#define LOOP_MALLOC_ERR (-21)
#define LOOP_TIMEOUT (-22)
#define LOOP_OVER_SIZE (-23)

#define IS_EVENT_IN(ev) ((ev).events&EPOLLIN)
#define IS_EVENT_OUT(ev) ((ev).events&EPOLLOUT)

#define IS_EVENT_ERR(ev) ((ev).events&EPOLLERR)
#define IS_EVENT_HUP(ev) ((ev).events&EPOLLHUP)

#define gettid() syscall(SYS_gettid)

typedef struct{
    int fd;
    int events;
}loop_event_t;

typedef struct loop_msg{
    int id;
    void* data;
    unsigned long when;          //ms
    loop_run_t run;

    struct loop_msg* next;
    struct loop_msg* prev;
}loop_msg_t;

typedef struct loop_item{
    int fd;
    loop_cbk_t on_data_in;
    loop_cbk_t on_data_out;
    loop_cbk_t on_error;

    struct loop_item* next;
}loop_item_t;

typedef struct _loop{
    char running;
    int fd;
    int tid;
    int evt_fd;
    int size;
    int count;
    int cur;
    volatile loop_item_t* items;
    pthread_mutex_t mutex;
    volatile loop_msg_t* msgs;
    volatile loop_handler_t handler;
    struct _loop* prev;
    struct _loop* next;
    struct epoll_event events[0];
}loop_t;

static loop_t _center = {
        mutex:PTHREAD_MUTEX_INITIALIZER,
        prev:&_center,
        next:&_center
};

static loop_t* center = &_center;

#define EVENTS(ev) ev.events
#define FD(ev) ev.data.fd

unsigned long loop_get_time(){
    struct timeb tb;
    unsigned long now;
    ( void ) ftime( &tb );

    now = ((tb.time%4290) * 1000000 ) + ( tb.millitm * 1000 );
    return (now);
}

static void loop_wake_done(void* loop){
    int fd = LOOP(loop)->evt_fd;
    long long tmp = 1;
    read(fd,&tmp,sizeof(tmp));
}
static void loop_wakeup(void* loop){
    int fd = LOOP(loop)->evt_fd;
    long long tmp = 1;
    write(fd,&tmp,sizeof(tmp));
}

static void loop_tail_msg(void* loop,loop_msg_t* msg){
    loop_t* _loop = loop;
    LOCK(loop);
    msg->next = (void*)_loop->msgs;
    msg->prev = _loop->msgs->prev;

    _loop->msgs->prev->next = msg;
    _loop->msgs->prev = msg;
    UNLOCK(loop);

    loop_wakeup(loop);
}

static void loop_push_msg(void* loop,loop_msg_t* msg){
    loop_t* _loop = loop;
    LOCK(loop);
    msg->prev =  (void*)_loop->msgs;
    msg->next = _loop->msgs->next;

    _loop->msgs->next->prev = msg;
    _loop->msgs->next = msg;

    UNLOCK(loop);

    loop_wakeup(loop);
}

static int loop_send_msg_run_at(void* loop,int id,void*data,loop_run_t run,unsigned long when,char tail){
    if(!loop)return LOOP_NULL;
    loop_t* _loop = loop;
    loop_msg_t* msg = malloc(sizeof(loop_msg_t));
    if(msg == NULL)return LOOP_MALLOC_ERR;
    memset(msg,0,sizeof(loop_msg_t));

    msg->id = id;
    msg->data = data;
    msg->when = when;
    msg->run = run;

    if(tail){
        loop_tail_msg(loop,msg);
    }else{
        loop_push_msg(loop,msg);
    }

    return 0;
}

int loop_post_at(void* loop,loop_run_t run,unsigned long when){
    return loop_send_msg_run_at(loop,0,NULL,run,when,1);
}

int loop_post_delay(void* loop,loop_run_t run,unsigned long delay){
    return loop_send_msg_run_at(loop,0,NULL,run,delay+loop_get_time(),1);
}

int loop_post(void* loop,loop_run_t run){
    return loop_send_msg_run_at(loop,0,NULL,run,loop_get_time(),0);
}

int loop_post_msg(void* loop,int id,void*data){
    return loop_send_msg_run_at(loop,id,data,NULL,loop_get_time(),0);
}

int loop_send_msg_delay(void* loop,int id,void* data,unsigned long delay){
    return loop_send_msg_run_at(loop,id,data,NULL,delay + loop_get_time(),1);
}

int loop_send_msg(void* loop,int id,void*data){
    return loop_send_msg_delay(loop,id,data,0);
}

static int loop_add_events(void* loop,int fd,int events){
    if(!loop)return LOOP_NULL;
    loop_t* _loop = loop;

    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl (_loop->fd, EPOLL_CTL_ADD, fd, &ev);
}

void* loop_new(int size){
    loop_t* loop = malloc(sizeof(loop_t) + sizeof(struct epoll_event)*size);
    if(loop == NULL)return NULL;
    memset(loop,0,sizeof(loop_t) + sizeof(struct epoll_event)*size);
    loop->msgs = malloc(sizeof(loop_msg_t));
    if(loop->msgs == NULL){
        free(loop);
        return NULL;
    }
    memset( (void*)loop->msgs,0,sizeof(loop_msg_t));
    loop->msgs->next = loop->msgs->prev =  (void*)loop->msgs;

    pthread_mutex_init(&loop->mutex,NULL);
    loop->fd =  epoll_create(size);
    if(loop->fd < 0){
        free(loop);
        return NULL;
    }
    loop->size = size;

    loop->evt_fd = eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
    loop_add_events(loop,loop->evt_fd,EPOLLIN);

    LOCK(center);
    loop->next = center;
    loop->prev = center->prev;
    center->prev->next = loop;
    center->prev = loop;
    UNLOCK(center);

    return loop;
}


static int loop_delete(void* loop){
    if(!loop)return LOOP_NULL;
    loop_t* _loop = loop;

    LOCK(center);
    _loop->prev->next = _loop->next;
    _loop->next->prev = _loop->prev;
    UNLOCK(center);

    close(_loop->evt_fd);
    close(_loop->fd);
    free(loop);

    return 0;
}

static int loop_wait_event(void* loop,loop_event_t* ev,int timeout){
    loop_t* _loop = loop;
    if((!loop)||(!ev)) return LOOP_NULL;

    if((_loop->count == 0)||(_loop->cur >= _loop->count)){
        _loop->cur = 0;
        _loop->count = epoll_wait (_loop->fd, _loop->events, _loop->size, timeout);
        if(_loop->count == 0)return LOOP_TIMEOUT;
    }

    ev->events = _loop->events[_loop->cur].events;
    ev->fd = _loop->events[_loop->cur].data.fd;
    _loop->cur += 1;
    return 0;
}

static void* loop_for_thread(pid_t tid){
    loop_t* ret = NULL;

    LOCK(center);
    loop_t* loop = center->next;

    while(loop != center){
        if(loop->tid == tid){
            ret = loop;
        }
        break;
    }
    UNLOCK(center);

    if(ret == NULL){
        ret = loop_new(loop_default_size);
        ret->tid = tid;
    }

    return ret;
}

pid_t loop_get_tid(void* loop){
    return LOOP(loop)->tid;
}

void* loop_get(){
    return loop_for_thread(gettid());
}

int loop_get_default_size(){
    return loop_default_size;
}

void loop_set_default_size(int size){
    loop_default_size = size;
}

loop_item_t* loop_item_for_fd(void* loop,int fd){
    if(!loop)return NULL;
    loop_t* _loop = loop;
    loop_item_t* item = (void*)_loop->items;
    while(item){
        if(item->fd == fd)return item;
        item = item->next;
    }
    return NULL;
}

int loop_register_fd(void* loop,int fd,loop_cbk_t on_data_in,loop_cbk_t on_data_out,loop_cbk_t on_error){
    loop_item_t* item = loop_item_for_fd(loop,fd);
    loop_t* _loop = loop;

    if(!item){
        LOCK(loop);
        if(!item){
            item = malloc(sizeof(loop_item_t));
            if(!item)return LOOP_MALLOC_ERR;
            memset(item,0,sizeof(loop_item_t));
            item->fd = fd;
            item->next = (void*)_loop->items;
            _loop->items = item;
        }
        UNLOCK(loop);
    }
    if(on_data_in)item->on_data_in = on_data_in;
    if(on_data_out)item->on_data_out = on_data_out;
    if(on_error)item->on_error = on_error;

    int events = 0;

    if(item->on_data_in)events |= EPOLLIN;
    if(item->on_data_out)events |= EPOLLOUT;
    if(item->on_error)events |= (EPOLLHUP|EPOLLERR);

    loop_add_events(loop,fd,events);

    return 0;
}

static void loop_run_once(void* loop){
    if(!loop)return;
    loop_event_t ev;
    loop_item_t* item;

    unsigned long now = loop_get_time();
    unsigned long when = 0xFFFFFFFF;
    loop_msg_t* msg = LOOP(loop)->msgs->next;
    int timeout = -1;

    while(msg != LOOP(loop)->msgs){
        if(msg->when <= now){
            if(msg->run)msg->run();
            else if(LOOP(loop)->handler)LOOP(loop)->handler(msg->id,msg->data);
            msg->prev->next = msg->next;
            msg->next->prev = msg->prev;
            msg = msg->next;
        }else{
            if(msg->when < when) when = msg->when;
            msg = msg->next;
        }
    }

    if(when > now)timeout = (int)(when - now);

    if(!loop_wait_event(loop,&ev,timeout)){
        if(ev.fd == LOOP(loop)->evt_fd){
            loop_wake_done(loop);
        }else{
            item = loop_item_for_fd(loop,ev.fd);
            if(!item)return;

            if(IS_EVENT_IN(ev) && item->on_data_in)item->on_data_in(ev.fd);
            else if(IS_EVENT_OUT(ev) && item->on_data_out)item->on_data_out(ev.fd);
            else if(IS_EVENT_ERR(ev) && item->on_error)item->on_error(ev.fd);
        }
    }
}

void loop_destroy(void* loop){
    loop_post(loop, ({void $this(void*p){
        LOOP(p)->running = 0;
    } $this;}));
}

void loop_loop(loop_handler_t handler){
    void* loop = loop_for_thread(gettid());

    LOOP(loop)->tid = gettid();
    LOOP(loop)->running = 1;
    while(LOOP(loop)->running){
        loop_run_once(loop);
    }
    loop_delete(loop);
}
