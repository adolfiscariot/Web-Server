#include <stdio.h>
#include <string.h> 
#include <stdlib.h> 
#include <sys/types.h>
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <netinet/in.h> 
#include <unistd.h> 

typedef struct{
	char method[8]; //GET, POST, etc
	char *path; // /info.html
	char *query_string; // ?pageNo=5
	char *protocol; // HTTP/1.1
	char *headers[20]; // Host: localhost:4040, Keep-alive: yes, Content-type: application/json etc
	int header_count; // # of headers
	char *body; // User in POST, PUT methods e.g form submissions
} HttpRequest;

//Parse Header
void parse_client_request(const char *raw_request_buffer, HttpRequest *client_request){
	HttpRequest request = {0}; //initialize all struct values to NULL;

	//duplicate request to avoid modifying the original
	char *duplicate_request_buffer = strdup(raw_request_buffer);
	if(duplicate_request_buffer == NULL)
	{
		perror("Memory allocation failed\n");
		exit(EXIT_FAILURE);
	}

	//fetch the HTTP Method
	char *request_line_token = strtok(duplicate_request_buffer, " \r\n");
	if(request_line_token != NULL)
	{
		/*
		 * Copy (at most) sizeof(client_request=>method) - 1 characters 
		 * from the request line into the client_request->method, 
		 * then manually set the null terminator
		 */
		strncpy(client_request->method, request_line_token, sizeof(client_request->method) - 1);
		client_request->method[sizeof(client_request->method) - 1] = '\0';
	}
	else 
	{
		fprintf(stderr, "Request line not found\n");
		free(duplicate_request_buffer); //strdup uses malloc so freeing this is a must.
		exit(EXIT_FAILURE);
	}

	//fetch the path
	char *path_token = strtok(NULL ," ");
	if (path_token != NULL)
	{
		/*
		 * Check if path contains query. If it does, store query to 
		 * client_request->query_string otherwise set it to null
		 */
		char *question_mark = strchr(path_token, '?');
		if (question_mark != NULL)
		{
			//replace question mark with null terminator
			*question_mark = '\0';
			//set path and query
			client_request->path = strdup(path_token);
			client_request->query_string = strdup(question_mark + 1);
		}
		else
		{
			client_request->path = strdup(path_token);
			client_request->query_string = NULL;	
		}
	}
	else
	{
		fprintf(stderr, "Path not found\n");
		free(duplicate_request_buffer);
		exit(EXIT_FAILURE);
	}

	//NEXT: FIND THE PROTOCOL
}

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

