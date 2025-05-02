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
		} else {
			buffer[valread] = '\0';
			printf("Received from client: %s\n", buffer);
		}

		//6. Send response.
//		char *hello = "HTTP/1.1 200 OK\r\n"
//			"Content-Type: text/plain\r\n"
//			"Content-Length: 10\r\n"
//			"\r\n"
//			"Sema Mbwa!";
//		write(client_socket, hello, strlen(hello));
//		printf("Message Sent To Client!\n");
//		close(client_socket);

		//6. Extract file name from request
		char *file_name = strtok(buffer, " "); //Points to GET
		file_name = strtok(NULL, " "); //Points to /info.html
		printf("%s\n", file_name);
		if (file_name[0] == '/')
			file_name++; //Points to info.html
			printf("%s\n", file_name);

		//7.Open the file
		FILE *fp = fopen(file_name, "r");
		if (fp == NULL){
			char *not_found = "HTTP/1.1 404 Not Found\r\n"
				"Content-Type: text/plain\r\n"
				"Content-Length: 13\r\n"
				"\r\n"
				"404 Not Found";
			write(client_socket, not_found, strlen(not_found));
		} else {
			//find out info.html's size
			fseek(fp, 0, SEEK_END);
			long file_size = ftell(fp);
			rewind(fp);
			
			//copy info.html into a buffer
			char *file_content = malloc(file_size + 1);
			if (fread(file_content, 1, file_size, fp) != file_size)
				perror("File read incomplete");
			fclose(fp);
			file_content[file_size] = '\0';

			//8. Build a Response header
			char header[1024];
			sprintf(header,
					"HTTP/1.1 200 OK\r\n"
					"Content-Type: text/html\r\n"
					"Content-Length: %ld\r\n"
					"Connection: close\r\n"
					"\r\n",
					file_size);

			//9. Send header then file content
			write(client_socket, header, strlen(header));
			write(client_socket, file_content, file_size);

			//10. Free the memory
			free(file_content);	
		}
		close(client_socket);

	}
	return 0;
}

