#include <stdio.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#define DEBUG			1

#define READNUM			1024
#define THREADNUM		10
#define IMAGEPATH		"images"
#define HTMLPATH		"htmlroot"
#define JSPATH			"javascript"


static int listen_sock = -1;

struct request_st
{
	int listen_fd;
	int accept_fd[THREADNUM];
	int head;
	int tail;
	pthread_cond_t cond;
	pthread_mutex_t mutex;
};

static struct request_st request;

void *work_thread(void *arg);
void send_http_error(int sock, int errorcode, const char *explain);
void send_http_data(int sock, const char *buf, int size);
void send_http_head(int sock);
int parserequest(const char *buf, char *method, char *url);
void dealrequest(int sock, const char *method, const char *url);

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
	request.listen_fd = listen_sock;
	request.head = request.tail = 0;
	pthread_cond_init(&request.cond, NULL);
	pthread_mutex_init(&request.mutex, NULL);
	for(i = 0; i < 10; ++i)
	{
		request.accept_fd[i] = -1;
	}

	for(i = 0; i < 10; ++i)
	{
		pthread_create(&threadid[i], NULL, work_thread, NULL);
	}

	while(1)
	{	
		FD_ZERO(&readfds);
		FD_SET(listen_sock, &readfds);
		timeout.tv_sec = 0;
		timeout.tv_usec = 1;
		if(select(listen_sock+1, &readfds, NULL, NULL, &timeout) < 0)
		{
			puts(strerror(errno));
			break;
		}
		if(FD_ISSET(listen_sock, &readfds))
		{
			accept_sock = accept(listen_sock, NULL, NULL);
			printf("Recive Request\n");
			if(accept_sock < 0)
			{
				puts(strerror(errno));
				break;
			}
			pthread_mutex_lock(&request.mutex);
			while((request.tail + 1)%THREADNUM == request.head)
			{
				pthread_cond_wait(&request.cond, &request.mutex);
			}	
			request.accept_fd[request.tail] = accept_sock;
			request.tail = (request.tail + 1)%THREADNUM;
			pthread_mutex_unlock(&request.mutex);
			pthread_cond_signal(&request.cond);
		}
	}
	for(i = 0; i < THREADNUM; ++i)
	{
		pthread_join(threadid[i], NULL);
	}
	close(listen_sock);
	pthread_cond_destroy(&request.cond);
	pthread_mutex_destroy(&request.mutex);
	return 0;
}


void *work_thread(void *arg)
{
	int sock = -1;
	char buf[1024] = {0};
	char method[50] = {0};
	char url[50] = {0};
	int result = -1;
	while(1)
	{
		memset(buf, 0x00, sizeof(buf));
		memset(method, 0x00, sizeof(method));
		memset(url, 0x00, sizeof(url));

		pthread_mutex_lock(&request.mutex);

		while(request.head == request.tail)
		{
			pthread_cond_wait(&request.cond, &request.mutex);
		}

		sock = request.accept_fd[request.head];
		request.head = (request.head + 1)%THREADNUM;

		pthread_mutex_unlock(&request.mutex);
		pthread_cond_signal(&request.cond);

		read(sock, buf, 1024);
#if DEBUG
		puts(buf);
#endif
		result = parserequest(buf, method, url);
		if(result != -1)
		{
			dealrequest(sock, method, url);
		}
		else
		{
			printf("send Error To browser\n");
			send_http_error(sock, 400, "Bad Request");
		}

		close(sock);
	}
	pthread_exit(NULL);
}

int parserequest(const char *buf, char *method, char *url)
{
	char tmpbuf[1024] = {0};
	const char *start = NULL, *end = NULL;
	if(NULL == buf || NULL == method || NULL == url)
	{
		printf("recive argument is NULL\n");
		return -1;
	}
	sprintf(method, "%s", "GET");

	if(NULL != (start = strchr(buf, '/')))
	{
		end = strchr(start, ' ');
		if(NULL != end)
		{
			strcat(tmpbuf, HTMLPATH);
			strncat(tmpbuf, start, end-start+1);
			snprintf(url, strlen(tmpbuf), "%s", tmpbuf);
		}
		else
		{
			return -1;
		}
	}
	else
	{
		return -1;
	}
	return 1;
}
void dealrequest(int sock, const char *method, const char *url)
{
	int filefd = -1;
	int cnt = 0;
	int writecnt = 0;
	int ret = 0;
	const char *filepath = NULL;
	char buf[1024] = {0};
	if(sock < 0 || NULL == method || NULL == url)
	{
		printf("%s Recive Error Argument\n", __FUNCTION__);
		return;
	}
	if(!strcmp("GET", method))
	{
		if(!strcmp(url, "htmlroot/"))
		{
			filepath = "htmlroot/index.html";
		}
		else
		{
			filepath = url;
		}
#if DEBUG
		puts(filepath);
#endif
		filefd = open(filepath, O_RDONLY);
		if(filefd < 0)
		{
#if DEBUG
			puts("open File failed");
#endif
			send_http_error(sock, 404, "Not Found");

			return;
		}
		send_http_head(sock);
		while(1)
		{
			memset(buf, 0x00, sizeof(buf));
			ret = read(filefd, buf, sizeof(buf) - 1);
			if(ret < 0)
			{
				puts(strerror(errno));
				break;
			}
			if(0 == ret)
				break;

			send_http_data(sock, buf, ret);
		}
	}
}

void send_http_error(int sock, int errorcode, const char *explain)
{
	if(sock < 0)
	{
		return;
	}
	int ret = 0;
	int cnt = 0;
	char buf[1024] = {0};
	strcat(buf, "HTTP/1.1 ");
	sprintf(buf+strlen(buf), "%d ", errorcode);
	sprintf(buf+strlen(buf), "%s\r\n\r\n", explain);

	while(cnt < strlen(buf))
	{
		ret = write(sock, buf+cnt, strlen(buf)-cnt);
		if(ret < 0)
		{
			puts(strerror(errno));
			break;
		}
		cnt += ret;
	}
}
void send_http_head(int sock)
{
	if(sock < 0)
	{
		return; 
	}
	char buf[1024] = "HTTP/1.1 200 OK\r\n\r\n";
	int cnt = 0, ret = 0;
	while(cnt < strlen(buf))
	{
		ret = write(sock, buf + cnt, strlen(buf) - cnt);
		if(ret < 0)
		{
			puts(strerror(errno));
			break;
		}
		cnt += ret;
	}
}
void send_http_data(int sock, const char *buf, int size)
{
	if(sock < 0 || NULL == buf)
	{
		printf("%s Recive Argument Error\n", __FUNCTION__);
		return ;
	}
	int ret = 0, cnt = 0;
#if DEBUG
	puts(buf);
#endif
	while(cnt < size)
	{
		ret = write(sock, buf+cnt, size-cnt);
		if(ret < 0)
		{
			puts(strerror(errno));
			break;
		}
		cnt += ret;
	}
}
