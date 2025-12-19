#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

#define PORT 4040

int main(void){
	struct addrinfo hints, *results;
	int x = 0;
	memset((void *)&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	char service[sizeof(PORT)];
	snprintf(service, sizeof(PORT), "%d", PORT);
	if (getaddrinfo(NULL, "4040", &hints, &results) != 0){
		perror("Failed to get address info");
		return 1;
	}
	
	printf("%s\n",service);
	

	return 0;
}
