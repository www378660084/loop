#include <stdio.h>         
#include <stdlib.h>     
#include <unistd.h>       
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>      
#include <termios.h>     
#include <errno.h>       
#include <pthread.h>
#include "loop.h"

int fd;

void on_data_in(int _fd){
    char buffer[256];
    int count;

    if(_fd == fd){
        count = read(fd,buffer,sizeof(buffer));
        if(count > 0)write(STDOUT_FILENO,buffer,count);
    }else if(_fd == STDIN_FILENO){
        count = read(STDIN_FILENO,buffer,sizeof(buffer));
        if(count > 0)write(fd,buffer,count);
    }else{
        printf("unkown fd:%d\n",_fd);
    }
}

#define VIRTUAL_SERIAL "/dev/ttyS1"

int main(int argc,char* argv[]) {
	if(argc < 2){
	    printf("open %s\n",VIRTUAL_SERIAL);
		fd = open(VIRTUAL_SERIAL, O_RDWR|O_NOCTTY);
	}else{
	    printf("open %s\n",argv[1]);
		fd = open(argv[1], O_RDWR|O_NOCTTY);
	}
	if (fd == -1) {
		printf("error openning %s\n",VIRTUAL_SERIAL);
		return -1;
	}
	struct termios opt; 

	if (tcgetattr(fd, &opt) != 0) {
		printf("tcgetattr fd");
		return -EXIT_FAILURE;
	}
	if(argc > 2){
		cfsetispeed(&opt, B460800);
		cfsetospeed(&opt,B460800);
	}else{
		cfsetispeed(&opt, B921600);
		cfsetospeed(&opt, B921600);
	}
	opt.c_cflag &= ~CSIZE;
	opt.c_cflag |= CS8;

	opt.c_cflag &= ~PARENB;       
	opt.c_iflag &= ~INPCK;     

	opt.c_cflag &= ~CSTOPB;
	opt.c_cflag |= (CLOCAL | CREAD);

	opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

	opt.c_oflag &= ~OPOST;
	opt.c_oflag &= ~(ONLCR | OCRNL);  

	opt.c_iflag &= ~(ICRNL | INLCR);
	opt.c_iflag &= ~(IXON | IXOFF | IXANY); 

	tcflush(fd, TCIFLUSH);
	opt.c_cc[VTIME] = 0;      
	opt.c_cc[VMIN] = 1;     
	printf("welcome to serial assistant\n");

	if(tcsetattr(fd, TCSANOW, &opt) != 0)
	{
		perror("tcsetattr fd");
		return -EXIT_FAILURE;
	}
	tcflush(fd, TCIFLUSH);

	void* loop = loop_get();

	loop_register_fd(loop,fd,on_data_in,NULL,NULL);
	loop_register_fd(loop,STDIN_FILENO,on_data_in,NULL,NULL);

	loop_loop(NULL);

	return EXIT_SUCCESS;
}

