#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#define SOCKET_LISTEN 0
#define SOCKET_REMOTE 1

struct remote
{
	char *address;
	char *port;
};

/*  global variables  */

struct pollfd *pollTab;
struct remote *rmt; 				/*tablica uzyta do przechowywania adresow pod ktore beda przekazane informacje*/
/**********************/

void error(const char * txt)
{
	fprintf(stderr, "%s\n", txt);

	exit(1);
}

void closeProgram()
{
	free(pollTab);
	free(rmt);

	exit(0);
}

void conveyMsg(int sfdSender, int sfdReceiver)
{
	char buffer[10000];
	ssize_t bytes;

	if((bytes = recv(sfdSender, buffer, sizeof(buffer), 0)) < 0)
		error("blad funkcji recv");	

	if(send(sfdReceiver, buffer, bytes, 0)==-1)
		error("blad funkcji send");
	
}

int createSocket(char *address, char *port, int flg)
{
	int sfd; 		/*deskryptor socketu, ktory jest tworzony*/
	struct addrinfo hints, *results;
	struct addrinfo *i;
	int setOption = 1; 	/*potrzebna do funkcji setsockopt*/
	int getAddrRes; 	/*przechowuje kod bledu funkcji getaddrinfo*/
	int flags; 		/*potrzebna do fukcji fcntl - ustawienie gniazda w tryb nieblokujacy*/



	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if(flg==SOCKET_LISTEN)
		hints.ai_flags = AI_PASSIVE;

	
	if(flg==SOCKET_LISTEN){
		if((getAddrRes = getaddrinfo(NULL, port, &hints, &results)) != 0)
			error(gai_strerror(getAddrRes));
	}
	else if(flg==SOCKET_REMOTE){
		if((getAddrRes = getaddrinfo(address, port, &hints, &results)) != 0)
			error(gai_strerror(getAddrRes));
	}

	/*iterowanie po strukturze results zwroconej przez getaddrinfo*/
	for(i = results; i!=NULL; i = i->ai_next){
		/* SOCKET */		
		if((sfd = socket(i->ai_family, i->ai_socktype, i->ai_protocol)) == -1)
			error("blad funkcji socket");

		if(setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &setOption, sizeof(int)) == -1)
			error("blad funkcji setsockopt");

		/* BIND LUB CONNECT */
		if(flg==SOCKET_LISTEN){		
			if(bind(sfd, i->ai_addr, i->ai_addrlen) == -1){
				close(sfd);
				perror("bind");
				continue;
			}
		} else if(flg == SOCKET_REMOTE){
			if(connect(sfd, i->ai_addr, i->ai_addrlen) == -1){
				close(sfd);
				perror("connection failure");
				continue;
			}
		}
			
		break;
	}

	if(i == NULL)
		error("bind");

	/* ustawienie gniazda w tryb nieblokujacy */
	flags = fcntl(sfd, F_GETFL, 0);
    	fcntl(sfd, F_SETFL, flags | O_NONBLOCK);
			

	if(flg == SOCKET_LISTEN){	
		if(listen(sfd, 10) == -1)
			error("listen");
	}

	freeaddrinfo(results);
	
	return sfd;
}

int main(int argc, char *argv[])
{
	
	char *listenPort; 			/*port nasluchujacy*/
	char *foreignAddress;
	char *foreignPort;
	int pollRes;
	int pollTabLen;
	int argLen = argc - 1;
	int i;

	signal(SIGINT, closeProgram);

	pollTab = (struct pollfd *)calloc( argLen, sizeof(struct pollfd) );
	rmt = (struct remote*)calloc(argLen, sizeof(struct remote));

	for(i = 1; i<argc; i++){
		/*uzyskanie danych z argumentow programu*/			
		listenPort = strtok(argv[i], ":");
		foreignAddress = strtok(NULL, ":");
		foreignPort = strtok(NULL, ":");			


		/*twozrzone sa sockety nasluchujace*/
		pollTab[i-1].fd = createSocket(NULL, listenPort, SOCKET_LISTEN);
		pollTab[i-1].events = POLLIN | POLLRDHUP;

		rmt[i-1].address = foreignAddress;
		rmt[i-1].port = foreignPort;
	}
	pollTabLen = argLen;

	while(1){
		/*******************************************************/
		/*                      POLL                           */
		/*******************************************************/
		pollRes = poll(pollTab, pollTabLen, 5*60*1000);

		if(pollRes==-1){
			error("poll");
		}
		else if(pollRes==0){
			printf("\ntimeout\n");
			exit(1);
		}

		/*przeszukiwanie po pollu*/
		for(i = 0; i<pollTabLen; i++){
			/**********************************************************************/
			/*                             POLLIN                                 */
			/**********************************************************************/
			if( (pollTab[i].revents & POLLIN)==POLLIN ){
				/*pojawil sie nowy klient*/					
				if(i<argLen){		
					/*uzycie funkcji realloc - potrzebne jest wiecej pamieci*/
					pollTab = (struct pollfd *)realloc( pollTab, (pollTabLen+2)*sizeof(struct pollfd) );
					if(pollTab == NULL)
						error("pollTab");


					/*komunikacja z klientem, ktory przyslal nam zadanie*/
					pollTab[pollTabLen].fd = accept(pollTab[i].fd, NULL, NULL);
					
					/*obsluga bledow funkcji accept*/
					if(pollTab[pollTabLen].fd==-1)
						error("accept");

					pollTab[pollTabLen].events = POLLIN | POLLRDHUP;

							
					/*komunikacja z maszyna, pod ktorej adres przekierowywujemy dane jako pierwsze*/
					pollTab[pollTabLen+1].fd = createSocket(rmt[i].address, rmt[i].port, SOCKET_REMOTE);
					pollTab[pollTabLen+1].events = POLLIN | POLLRDHUP;

					pollTabLen += 2;											
				}
				else if( (i-argLen+1)%2==1 ) 	/*klient, ktory sie z nami polaczyl jest gotowy do operacji I/O*/{			
					conveyMsg(pollTab[i].fd, pollTab[i+1].fd);
				}
				else if( (i-argLen+1)%2==0 ) 	/*maszyna z ktora sie polaczylismy jest gotowa do operacji I/O*/{				
					conveyMsg(pollTab[i].fd, pollTab[i-1].fd);
				}
			}

			/**********************************************************************/
			/*                            POLLRDHUP                               */
			/**********************************************************************/

			/*obsluga zerwania polaczenia*/
			if( (pollTab[i].revents & POLLRDHUP)==POLLRDHUP ){
				if( (i-argLen+1)%2==1 ){						
					pollTab[i].fd = -1;
					pollTab[i+1].fd = -1;
				}

				if( (i-argLen+1)%2==0 ){
					pollTab[i].fd = -1;
					pollTab[i-1].fd = -1;
				}
			}					
		}
	}


	return 0;
}
