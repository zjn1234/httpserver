#include <stdio.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


#define READNUM		1024
#define THREADNUM	10

static int listen_sock = -1;
struct request
{
	int listen_fd;
	int accept_fd[THREADNUM];
	int sp;
	pthread_cond_t cond;
	pthread_mutex_t mutex;
};

struct request re;

void *work_thread(void *arg);

int main(int argc, char *argv[])
{
	int i = 0;
	int accept_sock;
	struct sockaddr_in localaddr, remoteaddr;
	socklen_t socklen = 0;
	char buf[READNUM];
	ssize_t read_len = 0;
	int optval = 1;
	fd_set readfds,writefds;
	struct timeval timeout;
	pthread_t threadid[THREADNUM];

	listen_sock = socket(AF_INET, SOCK_STREAM, 0);

	if(-1 == listen_sock)
	{
		puts(strerror(errno));
		return -1;
	}
	localaddr.sin_family = AF_INET;
	localaddr.sin_port = htons(8045);
	inet_pton(AF_INET, "0.0.0.0", &(localaddr.sin_addr.s_addr));
	socklen = sizeof(struct sockaddr_in);
	if(bind(listen_sock, (struct sockaddr*)&localaddr, socklen) < 0)
	{
		puts(strerror(errno));
		close(listen_sock);
		return -1;
	}
	if(setsockopt(listen_sock, SOL_SOCKET,SO_REUSEADDR, &optval, sizeof(optval)) < 0)
	{
		puts(strerror(errno));
		close(listen_sock);
		return -1;
	}
	
	if(listen(listen_sock, 20) == -1)
	{
		puts(strerror(errno));
		close(listen_sock);	
		return -1;
	}
	//init request
	re.listen_fd = listen_sock;
	re.sp = 0;
	pthread_cond_init(&re.cond, NULL);
	pthread_mutex_init(&re.mutex, NULL);
	for(i = 0; i < 10; ++i)
	{
		re.accept_fd[i] = -1;
	}
	
	for(i = 0; i < 10; ++i)
	{
		pthread_create(&threadid[i], NULL, work_thread, NULL);
	}
	
	while(1)
	{	
		puts("waiting............\n");
		FD_ZERO(&readfds);
		FD_SET(listen_sock, &readfds);
		timeout.tv_sec = 1;
		timeout.tv_usec = 1;
		if(select(listen_sock+1, &readfds, NULL, NULL, &timeout) < 0)
		{
			puts(strerror(errno));
			break;
		}
		if(FD_ISSET(listen_sock, &readfds))
		{
			accept_sock = accept(listen_sock, NULL, NULL);
			if(accept_sock < 0)
			{
				puts(strerror(errno));
				break;
			}
			pthread_mutex_lock(&re.mutex);
			while(re.sp >= THREADNUM)
			{
				pthread_cond_wait(&re.cond, &re.mutex);
			}	
			re.accept_fd[++re.sp] = accept_sock;
			pthread_mutex_unlock(&re.mutex);
			pthread_cond_signal(&re.cond);
		}
	}
	
	
	close(listen_sock);
	return 0;
}


void *work_thread(void *arg)
{
	int sock = -1;
	char buf[1024] = {0};
	while(1)
	{
			pthread_mutex_lock(&re.mutex);

			while(re.sp <= 0)
			{
					pthread_cond_wait(&re.cond, &re.mutex);
			}

			sock = re.accept_fd[re.sp--];

			pthread_mutex_unlock(&re.mutex);
			pthread_cond_signal(&re.cond);

			do
			{
				memset(buf, 0x00, sizeof(buf));
				read(sock, buf, 1024);
				puts(buf);
				if(strstr(buf, "close") != NULL)
				{
					break;
				}
			}while(1);

			close(sock);
	}
}
