#include <stdio.h>
#include <string.h> 
#include <stdlib.h> 
#include <sys/types.h>
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <netinet/in.h> 
#include <unistd.h> 
#include <dirent.h>
#include <sys/wait.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <time.h>

#define OPEN_MAX 10 //Max number of forks

//Header struct 
typedef struct{
	char *key;
	char *value;
}Header;

typedef struct{ //Ordered from largest to smallest for better cache alignment
	Header headers[20]; // Host: localhost:4040, Keep-alive: yes, Content-type: application/json etc
	char *path; // /info.html
	char *query_string; // ?pageNo=5
	char *protocol; // HTTP/1.1
	char *body; // Used in POST, PUT methods e.g form submissions
	char method[8]; //GET, POST, etc
	int header_count; // # of headers
} HttpRequest;

//Semaphore global declaration
sem_t *semaphore;

//Parse Header
int parse_client_request(const char *raw_request_buffer, HttpRequest *client_request, char *request_line_end){
	HttpRequest request = {0}; //initialize all struct values to NULL;

	//2. Fetch the HTTP Method
	char *request_method_token = strtok((char *)raw_request_buffer, " \r\n");
	if(request_method_token != NULL)
	{
		/*
		 * Copy (at most) sizeof(client_request=>method) - 1 characters 
		 * from the request line into the client_request->method, 
		 * then manually set the null terminator
		 */
		strncpy(client_request->method, request_method_token, sizeof(client_request->method) - 1);
		client_request->method[sizeof(client_request->method) - 1] = '\0';
	}
	else 
	{
		fprintf(stderr, "Request line not found\n");
		return 1;
	}

	//3. Fetch the path and query string
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
			client_request->path = path_token;
			client_request->query_string = question_mark + 1;
		}
		else
		{
			client_request->path = path_token;
			client_request->query_string = NULL;	
		}
	}
	else
	{
		fprintf(stderr, "Path not found\n");
		return 1;
	}

	//4. Fetch the protocol
	char *protocol_token = strtok(NULL, " \r\n");
	if (protocol_token != NULL)
	{
		client_request->protocol = protocol_token;
		if (client_request->protocol == NULL)
		{
			fprintf(stderr, "Memory allocation failed\n");
			return 1;
		}
	}
	else
	{
		fprintf(stderr, "Protocol Not Found\n");
		return 1;
	}

	//5. Fetch the headers
	/*
	 * First we find the end of the request line then
	 * we use that to find the beginning of the headers. We then
	 * find the end of the headers and replace the delimiters with 
	 * a null terminating character. Afterwards we eliminate any 
	 * invalid headers then parse through the headers storing them
	 * to the headers field. Finally we increament header_count by 1.
	 */

	if (request_line_end == NULL)
	{
		fprintf(stderr, "Malformed request line\n");
		return 1;
	}

	char *headers_end = strstr(request_line_end + 2, "\r\n\r\n");
	if (headers_end == NULL)
	{
		fprintf(stderr, "Malformed headers - missing \\r\\n\\r\\n terminators.\n");
		return 1;
	}

	*headers_end = '\0';
	char *raw_headers_block = request_line_end + 2;

	char *header_token;
	char *save_ptr;

	header_token = strtok_r(raw_headers_block, "\r\n", &save_ptr);
	while (header_token != NULL)
	{
		if(client_request->header_count >= 20)
		{
			fprintf(stderr, "Max number of headers (20) reached\n");
			break;
		}
		if(strlen(header_token) > 0)
		{
			//A header must have a colon & if not, skip it
			char *colon = strchr(header_token, ':');
			if (colon == NULL)
			{
				fprintf(stderr, "Colon not found in header\n");
				header_token = strtok_r(NULL, "\r\n", &save_ptr);
				continue;
			}

			//Separate header key and value based on the colon 
			*colon = '\0';
			char *key = header_token;
			char *value = colon + 1;

			//Ensure whitespaces e.g. "Host: localhost" are eliminated
			while(*value == ' ' || *value == '\t')
			{
				value++;
			}
			 
			//Store key and value
			client_request->headers[client_request->header_count].key = strdup(key);
			client_request->headers[client_request->header_count].value = strdup(value);

			if
			(
				client_request->headers[client_request->header_count].key == NULL ||
				client_request->headers[client_request->header_count].value == NULL 
			)
			{
				perror("Memory allocation failed for header\n");
				return 1;
			}
			client_request->header_count++;
		}
		else
		{
			printf("Empty header token encountered. Skipping\n");
		}
		
		header_token = strtok_r(NULL, "\r\n", &save_ptr);
	}
	printf("Header parsing done. %d headers found\n", client_request->header_count);
	return 0;
}

void free_http_request(HttpRequest *request){
	for (int i = 0; i < request->header_count; i++){
		if (request->headers[i].key){
			free(request->headers[i].key);
		}
		if (request->headers[i].value){
			free(request->headers[i].value);
		}
	}
}

//Function to get header fields
char *get_header_value(HttpRequest *request, char *name){
	for (int i = 0; i < request->header_count; i++){
		if (strncasecmp(request->headers[i].key, name, strlen(name)) == 0){
			return request->headers[i].value;
		}
	}
	return NULL;
}

//Function to handle connection status. Returns 0 (close) or 1 (keep alive)
int connection_close_or_keep_alive(HttpRequest *client_request){
	int keep_alive = 0; /* Initialize keep alive to be 0 */
	
	//Get the request's connection status
	char *request_connection_status = get_header_value(client_request, "Connection");	

	//Get the request's protocol. HTTP/1.0 deafult = close. HTTP/1.1 default = keep-alive.
	char *protocol = client_request->protocol;
	if (strcmp (protocol, "HTTP/1.0") == 0){
		if (request_connection_status != NULL && strcasecmp(request_connection_status, "keep-alive") == 0) {
			keep_alive = 1;
		} else {
			keep_alive = 0;
		}
	} else if(strcmp (protocol, "HTTP/1.1") == 0){
		if (request_connection_status != NULL && strcasecmp(request_connection_status, "close") == 0){
			keep_alive = 0;
		} else {
			keep_alive = 1;
		}

	} else {
		keep_alive = 0;

	}

	return keep_alive;
}

//Function to handle the request method. Returns 0 for success, 1 for failure
int handle_method(int client_socket, HttpRequest *client_request, char *buffer, int bytes_read){
	printf("Handling the request....\n");

	if (strcmp(client_request->method, "GET") == 0)
	{
		//Canonical path for where files are
		const char *directory_name = "files";
		char canonical_directory_path[PATH_MAX];
		if (realpath(directory_name, canonical_directory_path) == NULL){
			fprintf(stderr, "Failed to canonicalize directory path\n");
			return 1;
		}
		char *request_path = client_request->path;
		
		//Actual file path on disk
		char *final_request_path;
	
		//1. Handle root requests
		if (strcmp(request_path, "/") == 0 || strcmp(request_path, "") == 0){
			final_request_path = "/index.html";
		} else {
			final_request_path = request_path;
		}

		//2. Dynamically allocate memory for full path
		size_t uncanonical_full_path_len = strlen(directory_name) + strlen(final_request_path) + 1;
		char *uncanonical_full_path = malloc(uncanonical_full_path_len);
		if (uncanonical_full_path == NULL){
			perror("Memory allocation failed\n");
			return 1;
		}

		//3. Construct full path and canonicalize it
		snprintf(uncanonical_full_path, uncanonical_full_path_len, "%s%s", directory_name, final_request_path);

		char *full_path = malloc(PATH_MAX);
		if (realpath(uncanonical_full_path, full_path) == NULL){
			fprintf(stderr, "Path canonicalization failed: %s\n", full_path);
			free(uncanonical_full_path);
			free(full_path);
			char *not_found = "HTTP/1.1 404 Not Found\r\n"
				"Content-Type: text/plain; charset=utf-8\r\n"
				"Content-Length: 15\r\n"
				"\r\n"
				"404 Not Found\r\n";
			write(client_socket, not_found, strlen(not_found));
			return 1;
		}

		printf("THE FULL PATH IS: %s\n", full_path);

		//Validate the path
		if (strncmp(canonical_directory_path, full_path, strlen(canonical_directory_path)) != 0){
			fprintf(stderr, "Security: Malicious path attack attempted: %s\n", full_path);
			char *forbidden = "HTTP/1.1 403 Forbidden\r\n"
				"Content-Type: text/plain; charset=utf-8\r\n"
				"Content-Length: 11\r\n"
				"\r\n"
				"Forbidden\r\n";
			write(client_socket, forbidden, strlen(forbidden));
		}

		//4. Open file. Use rb because not every file will be text.
		FILE *fp = fopen(full_path, "rb");
		if (fp == NULL){
			perror("Failed to open file\n");
			char *not_found = "HTTP/1.1 404 Not Found\r\n"
				"Content-Type: text/plain; charset=utf-8\r\n"
				"Content-Length: 15\r\n"
				"\r\n"
				"404 Not Found\r\n";
			write(client_socket, not_found, strlen(not_found));
			free(uncanonical_full_path);
			free(full_path);
			return 1;
		} else {
			//find out file size
			fseek(fp, 0, SEEK_END);
			long file_size = ftell(fp);
			rewind(fp);
			
			//copy file into a buffer
			char *file_content = malloc(file_size + 1);

			if (file_content == NULL){
				perror("Memory allocation failed\n");
				fclose(fp);
				free(uncanonical_full_path);
				free(full_path);
				char *server_error = "HTTP/1.1 500 Internal Server Error\r\n"
					"Content-Type: text/plain; charset=utf-8\r\n"
					"Content-Length: 23\r\n"
					"\r\n"
					"Internal Server Error\r\n";
				write(client_socket, server_error, strlen(server_error));
				return 1;
			}

			if (fread(file_content, 1, file_size, fp) != file_size){
				perror("File read incomplete");
				free(file_content);
				fclose(fp);
				free(uncanonical_full_path);
				free(full_path);
				char *server_error = "HTTP/1.1 500 Internal Server Error\n"
					"Content-Type: text/plain; charset=utf-8\r\n"
					"Content-Length: 23\r\n"
					"\r\n"
					"Internal Server Error\r\n";
				write(client_socket, server_error, strlen(server_error));
				return 1;
			}
			
			//8. Determine content type in response header
			const char *content_type = "application/octet-stream"; //Default
			char *file_extension = strrchr(final_request_path, '.');
			if (file_extension != NULL && file_extension != final_request_path){
				file_extension++;
				if (strcmp(file_extension, "html") == 0) content_type = "text/html";
				else if (strcmp(file_extension, "css") == 0) content_type = "text/css";
				else if (strcmp(file_extension, "js") == 0) content_type = "application/js";
				else if (strcmp(file_extension, "json") == 0) content_type = "application/json";
				else if (strcmp(file_extension, "pdf") == 0) content_type = "application/pdf";
				else if (strcmp(file_extension, "png") == 0) content_type = "image/png";
				else if (strcmp(file_extension, "jpg") == 0 || strcmp(file_extension, "jpeg") == 0) content_type = "image/jpeg";
			}

			//9. Check the connection 
			int conn_status = connection_close_or_keep_alive(client_request);
			char *conn;
			if (conn_status == 0){
				conn = "close";
			}
			else {
				conn = "keep-alive";
			}

			//10. Build a Response header
			char header[1024];
			sprintf(header,
					"HTTP/1.1 200 OK\r\n"
					"Content-Type: %s\r\n"
					"Content-Length: %ld\r\n"
					"Connection: %s\r\n"
					"\r\n",
					content_type,
					file_size,
					conn);

			//10. Send header then file content
			write(client_socket, header, strlen(header));
			write(client_socket, file_content, file_size);

			//11. Free the memory
			free(file_content);	
			free(uncanonical_full_path);
			free(full_path);

			//12. Close the file
			fclose(fp);
		}
		printf("Request handling done\n");
		return 0;
	}

	//=====================POST METHOD==================
	/*
	 *Check if the method is post. Check content length and convert it to an integer. Read that 
	 *many bytes from the request body (after 'r/n/r/n') into a buffer. Perform different actions
	 *based on the content type. Send a response.
	 */
	if (strcmp(client_request->method, "POST") == 0){
		printf("Handling POST method...\n");

		//1. Get Content-Length header
		char *content_length_str = get_header_value(client_request, "Content-Length");
		if (content_length_str == NULL){
			perror("Content length not found in reqest header\n");
			char *no_content_length = "HTTP/1.1 400 Bad Request\r\n\r\n";
			write(client_socket, no_content_length, strlen(no_content_length));
			return 1;
		}

		//2. Convert str to int
		long content_length = atoi(content_length_str);
		if (content_length <= 0){
			perror("Invalid content length\n");
			char *no_content_length = "HTTP/1.1 400 Bad Request\r\n\r\n";
			write(client_socket, no_content_length, strlen(no_content_length));
			return 1;
		}
		printf("The content length is %lu\n", content_length);
		
		//3. Allocate memory for request body
		char *request_body = malloc(content_length + 1);
		if (request_body == NULL){
			perror("Failed to allocate memory\n");
			char *server_error = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
			write(client_socket, server_error, strlen(server_error));
			return 1;
		}
		printf("Allocated memory to request body\n");

		//4. Read bytes from buffer to request_body
		int total_bytes_read = 0;
		if (buffer != NULL && bytes_read > 0){
			int bytes_to_copy = (bytes_read < content_length) ? bytes_read : content_length;
			memcpy(request_body, buffer, bytes_to_copy);
			total_bytes_read = bytes_to_copy;
		}

		//If not all bytes have been read from the buffer then check for the remaining ones 
		//in the client socket
		while (total_bytes_read < content_length) {
			int bytes_read = read(client_socket, request_body + total_bytes_read, content_length - total_bytes_read);
			if (bytes_read <= 0){
				free(request_body);
				return 1;
			}
			total_bytes_read += bytes_read;
		}
		request_body[content_length] = '\0';
		printf("Content Length: %ld\n", content_length);
		printf("Request Body: %s\n", request_body);

		//5. Send success response
		char *success_response = "HTTP/1.1 200 OK\r\n"
			"Content-Type: text/plain; charset=utf-8\r\n"
			"Content-Length: 24\r\n"
			"\r\n"
			"POST request processed\r\n";

		int bytes_written = write(client_socket, success_response, strlen(success_response));
		printf("Bytes written: %d (expected %zu)\n", bytes_written, strlen(success_response));
		if (bytes_written < 0) 
		{
			perror("Write failed"); 
			return 1;
		}
		free(request_body);
		return 0;
	}

	else {
		fprintf(stderr, "Method Not Allowed\n");
		char *method_not_allowed = "HTTP/1.1 405 Method Not Allowed\r\n"
			"Content-Type: text/html; charset=utf-8\r\n"
			"Content-Length: 20\r\n"
			"\r\n"
			"Method Not Allowed\r\n";
		write(client_socket, method_not_allowed, strlen(method_not_allowed));
		return 1;
	}
	
}

//Signal handler method
void signal_handler(int sig){
	pid_t pid;
	int status;

	while((pid = waitpid(-1, &status, WNOHANG)) > 0){
		printf("Process %d has been terminated.\n", pid);
	}
}


int main(int argc, char *argv[]){
	//0. Get address info
	struct addrinfo hints, *results;

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	
	if(getaddrinfo(NULL, "4040", &hints, &results) != 0){
		perror("Failed to find address\n");
		return 1;
	}

	// 1. Create a socket
	int server_fd = socket(results->ai_family, results->ai_socktype, results->ai_protocol);
	if (server_fd < 0){
		perror("Cannot create socket\n");
		return 0;
		}
	
	// 2. Bind socket to address
	if (bind(server_fd, results->ai_addr, results->ai_addrlen) < 0){
		perror("Binding failed\n");
		return 0;
	}

	// 3. Listen for connections.
	int listen_for_connection = listen(server_fd, 5);
	if (listen_for_connection < 0){
		perror("Listening failed\n");
		exit(1);
	}
	printf("Server listening on port 4040...\n");

	//Call signal_handler when a child process terminates
	struct sigaction sa;
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if(sigaction(SIGCHLD, &sa, NULL) == -1){
		perror("Sigaction failed\n");
		close(server_fd);
		exit(1);
	}

	//Semaphore memory mapping
	semaphore = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (semaphore == MAP_FAILED){
		perror("Sempahore memory mapping failed\n");
		exit(1);
	}

	//Initialize semaphore with 10 max processes
	if(sem_init(semaphore, 1, OPEN_MAX) != 0){
		perror("Semaphore initialization failed\n");
		exit(1);
	}
	// 4. Accept connections.
	while(1){
		struct sockaddr_in client_addy;
		socklen_t client_addy_len = sizeof(client_addy);
		int client_socket = accept(server_fd, (struct sockaddr *) &client_addy, &client_addy_len);
		if (client_socket < 0){
			perror("Connection failed\n");
			continue;
		}

		//Print client's IP Address, port number and time of request
		time_t current_time = time(&current_time);
		struct tm *time_info = time_info = localtime(&current_time);

		char client_ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(client_addy.sin_addr), client_ip, INET_ADDRSTRLEN);
		printf("Client's IP Address: %s. Time: %s\n", client_ip, asctime(time_info));

		//Check if there's an available slot before creating a new child
		if (sem_wait(semaphore) != 0){
			perror("sem_wait failed\n");
			close(client_socket);
			continue;
		}

		//Create child process to handle client request
		pid_t pid = fork();

		if (pid == -1){
			perror("Fork failed\n");
			sem_post(semaphore);
			close(client_socket);
			continue;
		}

		if (pid == 0){
			
			//Child closes listening socket
			close(server_fd);

			int connection_status;
			do{
				// 5. Read data.
				char buffer[1024] = {0};
				int valread = read(client_socket, buffer, sizeof(buffer) - 1);
				if (valread ==  0){
					perror("End of file");
					sem_post(semaphore);
					close(client_socket);
					exit(1);
				}
				else if (valread < 0){
					perror("Read failed or empty request\n");
					sem_post(semaphore);
					close(client_socket);
					exit(1);
				}

				buffer[valread] = '\0';
				printf("Received from client: %s\n", buffer);

				//Find request body before parsing
				char *body_in_buffer = NULL;
				int body_bytes_in_buffer = 0;

				char *body_start = strstr(buffer, "\r\n\r\n");
				if (body_start != NULL){
					body_start += 4;
					body_bytes_in_buffer = valread - (body_start - buffer);
					if (body_bytes_in_buffer > 0){
						body_in_buffer = body_start;
					}
				}

				//6. Parse request header
				HttpRequest client_request = {0};
				char *request_line_end = strstr(buffer, "\r\n");
				int parse_result = parse_client_request(buffer, &client_request, request_line_end);
				if (parse_result != 0){
					const char *bad_result = "HTTP/1.1 400 Bad Request\r\n\r\n";
					sem_post(semaphore);
					close(client_socket);
					exit(0);
				}

				char *conn_header = get_header_value(&client_request, "Connection");

				//Handle the method
				int method_status = handle_method(client_socket, &client_request, body_in_buffer, body_bytes_in_buffer);
				if (method_status != 0){
					fprintf(stderr, "Request handling failed for client socket\n");
				}
				if (strcmp(client_request.method, "POST") == 0){
					printf("POST completed, closing connection\n");
					connection_status = 0;
				}
				else{
					//Determine the connection
					connection_status = connection_close_or_keep_alive(&client_request);
				}

				//Free HttpRequest data
				free_http_request(&client_request);

			}while(connection_status == 1);

			//Release the slot
			sem_post(semaphore);

			//Close client socket for child process
			close(client_socket);
			exit(0);
		}
		


		//Close client socket for parent process
		close(client_socket);
	}
	sem_destroy(semaphore);
	munmap(semaphore, sizeof(sem_t));
	close(server_fd);
	return 0;
}

