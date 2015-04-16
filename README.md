# loop
loop implementation


===================
example
-------------------
```c
void* loop = loop_get();
loop_register_fd(loop,fd,on_data_in,NULL,NULL);
loop_register_fd(loop,STDIN_FILENO,on_data_in,NULL,NULL);
loop_loop(NULL);
```

========
for file read write
---------------------
```c
typedef void (*loop_cbk_t)(int fd); 
int loop_register_fd(void* loop,int fd,loop_cbk_t on_data_in,loop_cbk_t on_data_out,loop_cbk_t on_error);
```
=======
for message send 
--------------
```c
int loop_send_msg(void* loop,int id,void*data);
int loop_send_msg_delay(void* loop,int id,void* data,unsigned long delay);
int loop_post_msg(void* loop,int id,void*data);
```
=======
message handler
--------------
```c
typedef void (*loop_handler_t)(int id,void* data);<br/>
void loop_loop(loop_handler_t handler);<br/>
```

=======
for run in loop thread
----------------
typedef void (*loop_run_t)();<br/>
int loop_post(void* loop,loop_run_t run);<br/>
int loop_post_delay(void* loop,loop_run_t run,unsigned long delay);<br/>
int loop_post_at(void* loop,loop_run_t run,unsigned long when);<br/>
