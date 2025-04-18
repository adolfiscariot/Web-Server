#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]){
	int client_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (client_fd < 0){
		perror("Socket creation failed\n");
		exit(EXIT_FAILURE);
	}
	
	struct sockaddr_in addy;
	memset(&addy, 0, sizeof(addy));
	addy.sin_family = AF_INET;
	addy.sin_port = htons(4040);
	if (inet_pton(AF_INET, "127.0.0.1", &addy.sin_addr) < 0) {
		printf("Invalid Address\n");
		exit(EXIT_FAILURE);
	}

	int connected = connect(client_fd, (struct sockaddr *)&addy, sizeof(addy));
	if (connected < 0){
		printf("Connection failed\n");
		exit(EXIT_FAILURE);
	}

	char *hello = "Oyaaaaaaa\n";
	send(client_fd, hello, strlen(hello), 0);
	printf("Umetuma oya\n");
	char buffer[1024];
	read(client_fd, buffer, sizeof(buffer) - 2);
	buffer[sizeof(buffer) - 1] = '\0';
	printf("%s\n", buffer);
	return 0;
}
