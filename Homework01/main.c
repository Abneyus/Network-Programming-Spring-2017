//Elijah Abney (abneye)
//Jacob Farnsworth (farnsj2)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <math.h>
#include <dns_sd.h>

int stopNow = 0;
int parentfd, childfd;
struct sockaddr_in sock_server, sock_client, sock_temp;
char msg[256];
int err;

void HandleEvents(DNSServiceRef serviceRef) {
	int dns_sd_fd = DNSServiceRefSockFD(serviceRef);
	int nfds = dns_sd_fd + 1;
	fd_set readfds;
	struct timeval tv;
	int i, read_len;

	while (!stopNow) {
		while(1) {
			int my_num = rand()%100 + 1;
			int guesses = 0;

			listen(parentfd, 1);

			i = sizeof(struct sockaddr_in);

			childfd = accept(parentfd, (struct sockaddr*)&sock_temp, (socklen_t*)&i);

			if(childfd < 0)
			{
				printf("accept() failure\n");
				exit(EXIT_FAILURE);
			}

			//client is connected.

			while((read_len = recv(childfd, msg, 256, 0)) > 0)
			{
				if(!strncasecmp(msg, "guess ", 6))
				{
					//client sent a guess
					char * szNum = msg + 6;
					int guess = atoi(szNum);

					++guesses;

					if(guess == my_num)
					{
						write(childfd, "CORRECT\n", strlen("CORRECT\n"));

						if(guesses < (log(100.0)/log(2.0) - 1))
						{
							write(childfd, "GREAT GUESSING\n", strlen("GREAT GUESSING\n"));
						}
						else if(guesses > (log(100.0)/log(2.0) + 1))
						{
							write(childfd, "BETTER LUCK NEXT TIME\n", strlen("BETTER LUCK NEXT TIME\n"));
						}
						else
						{
							write(childfd, "AVERAGE\n", strlen("AVERAGE\n"));
						}

						close(childfd);
						break;
					}
					else if(guess < my_num)
					{
						write(childfd, "GREATER\n", strlen("GREATER\n"));
					}
					else if(guess > my_num)
					{
						write(childfd, "SMALLER\n", strlen("SMALLER\n"));
					}
				}
				else
				{
					write(childfd, "???\n", strlen("???\n"));
				}
			}

			if(read_len == -1)
			{
				printf("recv() failure\n");
				exit(EXIT_FAILURE);
			}
		}
		int result = select(nfds, &readfds, (fd_set*)NULL, (fd_set*)NULL, &tv);

		if (result > 0) {
			if (FD_ISSET(dns_sd_fd, &readfds))
				err = DNSServiceProcessResult(serviceRef);
			if (err) stopNow = 1;
		}
	}
}

static void MyRegisterReply(DNSServiceRef sdRef, DNSServiceFlags flags,
  DNSServiceErrorType errorCode, const char *name, const char *regtype,
  const char *domain, void *context) {

}

int main(int argc, char **argv)
{
	int port;

	socklen_t sl;

	parentfd = socket(AF_INET, SOCK_STREAM, 0);
	if(parentfd == -1)
	{
		printf("socket() failure\n");
		return 0;
	}

	sock_server.sin_family = AF_INET;
	sock_server.sin_addr.s_addr = INADDR_ANY;
	sock_server.sin_port = htons(0);

	if(bind(parentfd, (struct sockaddr*)&sock_server, sizeof(sock_server)) < 0)
	{
		printf("bind() failure\n");
		return 0;
	}

	if(getsockname(parentfd, (struct sockaddr*)&sock_temp, &sl) < 0)
	{
		printf("getsockname() failure\n");
		return 0;
	}

	port = ntohs(sock_temp.sin_port);
	printf("Listening on %d\n", port);

  DNSServiceRef ref;
  int error = DNSServiceRegister(&ref, 0, 0, "abneye", "_gtn._tcp", NULL, NULL, sock_temp.sin_port, 0, NULL, MyRegisterReply, NULL);

  if(error == 0) {
    HandleEvents(ref);
  } else {
		printf("bonjour conf failed\n");
		return EXIT_FAILURE;
	}

	return 0;
}
