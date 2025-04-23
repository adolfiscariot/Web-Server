#include <stdio.h>
#include <string.h> 
#include <stdlib.h> 
#include <sys/types.h>
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <netinet/in.h> 
#include <unistd.h> 


int main(int argc, char *argv[]){
	// 1. Create a socket
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0){
		perror("Cannot create socket\n");
		return 0;
		}
	
	// 2. Bind socket to address
	struct sockaddr_in addy;
	memset((char *) &addy, 0, sizeof(addy));
	addy.sin_family = AF_INET;
	addy.sin_port = htons(4040);
	addy.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(server_fd, (struct sockaddr *) &addy, sizeof(addy)) < 0){
		perror("Binding failed\n");
		return 0;
	}

	// 3. Listen for connections.
	int listen_for_connection = listen(server_fd, 5);
	if (listen_for_connection < 0){
		perror("Listening failed\n");
		exit(EXIT_FAILURE);
	}
	printf("Server listening on port 4040...\n");

	// 4. Accept connections.
	while(1){
		struct sockaddr_in client_addy;
		socklen_t client_addy_len = sizeof(client_addy);
		int client_socket = accept(server_fd, (struct sockaddr *) &client_addy, &client_addy_len);
		if (client_socket < 0){
			perror("Connection failed\n");
			continue;
		}

		// 5. Read data.
		char buffer[1024] = {0};
		int valread = read(client_socket, buffer, sizeof(buffer) - 1);
		if (valread < 0){
			perror("Read failed\n");
			continue;
		} else
			printf("Received from client: %s\n", buffer);

		//6. Send response.
		char *hello = "HTTP/1.1 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Content-Length: 10\r\n"
			"\r\n"
			"Sema Mbwa!";
		write(client_socket, hello, strlen(hello));
		printf("Message Sent!\n");
		close(client_socket);
	}
	return 0;
}

