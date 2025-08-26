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

#define OPEN_MAX 10 //Max number of forks

typedef struct{ //Ordered from largest to smallest for better cache alignment
	char *headers[20]; // Host: localhost:4040, Keep-alive: yes, Content-type: application/json etc
	char *path; // /info.html
	char *query_string; // ?pageNo=5
	char *protocol; // HTTP/1.1
	char *body; // User in POST, PUT methods e.g form submissions
	char method[8]; //GET, POST, etc
	int header_count; // # of headers
} HttpRequest;

//Semaphore global declaration
sem_t *semaphore;

//Parse Header
int parse_client_request(const char *raw_request_buffer, HttpRequest *client_request, char *request_line_end){
	HttpRequest request = {0}; //initialize all struct values to NULL;

	//1. Duplicate request to avoid modifying the original
	char *duplicate_request_buffer = strdup(raw_request_buffer);
	if(duplicate_request_buffer == NULL)
	{
		fprintf(stderr, "Memory allocation failed\n");
		return 1;
	}

	//2. Fetch the HTTP Method
	char *request_method_token = strtok(duplicate_request_buffer, " \r\n");
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
		free(duplicate_request_buffer); //strdup uses malloc so freeing this is a must.
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
		return 1;
	}

	//4. Fetch the protocol
	char *protocol_token = strtok(NULL, " \r\n");
	if (protocol_token != NULL)
	{
		client_request->protocol = strdup(protocol_token);
		if (client_request->protocol == NULL)
		{
			fprintf(stderr, "Memory allocation failed\n");
			free(duplicate_request_buffer);
			return 1;
		}
	}
	else
	{
		fprintf(stderr, "Protocol Not Found\n");
		free(duplicate_request_buffer);
		return 1;
	}

	//5. Fetch the headers
	/*
	 * First we find the end of the request line then
	 * we use that to find the beginning of the headers. We then
	 * find the end of the headers and replace
	 * the delimiters with a null terminating character. Afterwards
	 * we tokenize the headers adding a pointer to 
	 * each header e.g "User Agent" to the headers field. Finally we
	 * increament header_count by 1.
	 */

	if (request_line_end == NULL)
	{
		fprintf(stderr, "Malformed request line\n");
		free(duplicate_request_buffer);
		return 1;
	}

	char *headers_end = strstr(request_line_end + 2, "\r\n\r\n");
	if (headers_end == NULL)
	{
		fprintf(stderr, "Malformed headers - missing \\r\\n\\r\\n terminators.\n");
		free(duplicate_request_buffer);
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
			client_request->headers[client_request->header_count] = strdup(header_token);
			if(
			client_request->headers[client_request->header_count] == NULL)
			{
				perror("Memory allocation failed\n");
				free(duplicate_request_buffer);
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
	free (duplicate_request_buffer);
	return 0;
}

//Function to get header fields
char *get_header_name(HttpRequest *request, char *name)
{
	//loop through headers:
	for (int i = 0; i < request->header_count; i++)
	{
		if (request->headers[i] == NULL)
		{
			printf("Header not found. Skipping\n");
			continue;
		}

		//A header must have a colon & if not, skip it
		char *colon = strchr(request->headers[i], ':');
		if (colon == NULL)
		{
			fprintf(stderr, "Colon not found in header\n");
			continue;
		}

		//The name length is from the beginning to the colon
		size_t name_length = colon - request->headers[i];

		//Return value if header in HttpRequest struct matches inputed header
		if (strncasecmp(request->headers[i], name, name_length) == 0 && name_length == strlen(name))
		{
			char *whitespaces = colon + 1;
			while(*whitespaces == ' ' || *whitespaces == '\t')
				whitespaces++;
			return whitespaces;
		}
	}
	printf("There are no more headers to loop through\n");
	return NULL;
}

//Function to handle connection status. Returns 0 (close) or 1 (keep alive)
int handle_connection(HttpRequest *client_request){
	int keep_alive = 0; /* Initialize keep alive to be 0 */
	
	//Get the request's connection status
	char *request_connection_status = get_header_name(client_request, "Connection");	

	//Get the request's protocol. HTTP/1.0 deafult = close. HTTP/1.1 default = keep-alive.
	char *protocol = client_request->protocol;
	if (strcmp (protocol, "HTTP/1.0") == 0){
		if (request_connection_status != NULL && strcasecmp(request_connection_status, "close") == 0) {
			keep_alive = 0;
		} else {
			keep_alive = 1;
		}
	} else if(strcmp (protocol, "HTTP/1.1") == 0){
		if (request_connection_status != NULL && strcasecmp(request_connection_status, "keep-alive") == 0){
			keep_alive = 1;
		} else {
			keep_alive = 0;
		}

	} else {
		keep_alive = 0;

	}

	return keep_alive;
}


//Function to handle the request method. Returns 0 for success, 1 for failure
int handle_method(int client_socket, HttpRequest *client_request){
	printf("Handling the method....\n");

	if (strcmp(client_request->method, "GET") == 0)
	{
		//path for where files are
		const char *directory_name = "files";
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
		size_t full_path_len = strlen(directory_name) + strlen(final_request_path) + 1;
		char *full_path = malloc(full_path_len);
		if (full_path == NULL){
			perror("Memory allocation failed\n");
			return 1;
		}

		//3. Construct full path
		snprintf(full_path, full_path_len, "%s%s", directory_name, final_request_path);

		//4. Open file. Use rb because not every file will be text.
		FILE *fp = fopen(full_path, "rb");
		if (fp == NULL){
			perror("Failed to open file\n");
			printf("Full path is %s\n", full_path);
			char *not_found = "HTTP/1.1 404 Not Found\r\n"
				"Content-Type: text/plain\r\n"
				"Content-Length: 13\r\n"
				"\r\n"
				"404 Not Found\r\n";
			write(client_socket, not_found, strlen(not_found));
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
				free(full_path);
				char *server_error = "HTTP/1.1 500 Internal Server Error\r\n"
					"Content-Type: text/plain\r\n"
					"Content-Length: 22\r\n"
					"\r\n"
					"Internal Server Error";
				write(client_socket, server_error, strlen(server_error));
				return 1;
			}

			if (fread(file_content, 1, file_size, fp) != file_size){
				perror("File read incomplete");
				free(file_content);
				fclose(fp);
				free(full_path);
				char *server_error = "HTTP/1.1 500 Internal Server Error\n"
					"Content-Type: text/plain\r\n"
					"Content-Length: 22\r\n"
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
			int conn_status = handle_connection(client_request);
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
			free(full_path);
		}
		printf("Method handled\n");
		return 0;
	}

	//=====================POST METHOD==================


	else {
		fprintf(stderr, "Method Not Allowed\n");
		char *method_not_allowed = "HTTP/1.1 405 Method Not Allowed\r\n"
			"Content-Type: text/html\r\n"
			"Content-Length: 18\r\n"
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
		exit(1);
	}
	printf("Server listening on port 4040...\n");

	//Call signal_handler when a child process terminates
	struct sigaction sa;
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if(sigaction(SIGCHLD, &sa, NULL) == -1){
		perror("Sigaction failed");
		close(server_fd);
		exit(1);
	}

	//Semaphore memory mapping
	semaphore = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (semaphore == MAP_FAILED){
		perror("Sempahore memory mapping failed");
		exit(1);
	}

	//Initialize semaphore with 10 max processes
	if(sem_init(semaphore, 1, OPEN_MAX) != 0){
		perror("Semaphore initialization failed");
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
		char client_ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(client_addy.sin_addr), client_ip, INET_ADDRSTRLEN);
		printf("Client's IP Address: %s\n", client_ip);

		//Check if there's an available slot before creating a new child
		if (sem_wait(semaphore) != 0){
			perror("sem_wait failed");
			close(client_socket);
			continue;
		}

		//Create child process to handle client request
		pid_t pid = fork();

		if (pid == -1){
			perror("Fork failed");
			sem_post(semaphore);
			close(client_socket);
			continue;
		}

		if (pid == 0){
			
			//Child closes listening socket
			close(server_fd);
			
			// 5. Read data.
			char buffer[1024] = {0};
			int valread = read(client_socket, buffer, sizeof(buffer) - 1);
			if (valread <= 0){
				perror("Read failed or empty request");
				sem_post(semaphore);
				close(client_socket);
				exit(1);
			}

			buffer[valread] = '\0';
			printf("Received from client: %s\n", buffer);

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

			//Handle the method
			int method_status = handle_method(client_socket, &client_request);
			if (method_status != 0){
				fprintf(stderr, "Request handling failed for client socket\n");
			}

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

