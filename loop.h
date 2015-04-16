/*
 * loop.h
 *
 *  Created on: 2015-3-27
 *      Author: Administrator
 */

#ifndef ___LOOP_H_
#define ___LOOP_H_
#include <sys/epoll.h>
#include <pthread.h>

typedef void (*loop_run_t)();
typedef void (*loop_handler_t)(int id,void* data);
typedef void (*loop_cbk_t)(int fd);

void* loop_get();
void loop_loop(loop_handler_t handler);
void loop_destroy(void* loop);
pid_t loop_get_tid(void* loop);

int loop_register_fd(void* loop,int fd,loop_cbk_t on_data_in,loop_cbk_t on_data_out,loop_cbk_t on_error);

int loop_send_msg(void* loop,int id,void*data);
int loop_send_msg_delay(void* loop,int id,void* data,unsigned long delay);

int loop_post_msg(void* loop,int id,void*data);
int loop_post(void* loop,loop_run_t run);
int loop_post_delay(void* loop,loop_run_t run,unsigned long delay);
int loop_post_at(void* loop,loop_run_t run,unsigned long when);

unsigned long loop_get_time();


#endif /* ___LOOP_H_ */
