#include <stdio.h>
#include <string.h> 
#include <stdlib.h> 
#include <sys/types.h>
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <netinet/in.h> 
#include <unistd.h> 
#include <dirent.h>

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
	 * First we find the end of the request line using strstr then
	 * we use that to find the beginning of the headers. We then
	 * find the end of the headers using strstr again and replace
	 * the delimiters with a null terminating character. Afterwards
	 * we tokenize the headers using strtok, adding a pointer to 
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
		if (strcmp(request_path, "/") == 0){
			final_request_path = "/index.html"
		} else {
			final_request_path = request_path;
		}

		//2. Dynamically allocate memory for full path
		size_t full_path_len = strlen(directory_name) + strlen(request_path) + 1;
		char *full_path = malloc(full_path_len);
		if (full_path == NULL){
			perror("Memory allocation failed\n");
			free (duplicate_request_buffer);
			return 1;
		}

		//3. Construct full path
		snprintf(full_path, full_path_len, "%s%s", directory_name, final_request_path);

		//4. Open file. Use rb because not every file will be text.
		FILE *fp = fopen(full_path, "rb");
		if (fp == NULL){
			perror("Failed to open file\n");
			char *not_found = "HTTP/1.1 404 Not Found\r\n"
				"Content-Type: text/plain\r\n"
				"Content-Length: 13\r\n"
				"\r\n"
				"404 Not Found";
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
				wirte(client_socket, server_error, strlen(server_error);
				return 1;
			}

			if (fread(file_content, 1, file_size, fp) != file_size){
				perror("File read incomplete");
				free(file_content);
				fclose(fp);
				free(full_path);
				char *server_error = "HTTP/1.1 500 Internal Server Error\r\n"
					"Content-Type: text/plain\r\n"
					"Content-Length: 22\r\n"
					"\r\n"
					"Internal Server Error";
				wirte(client_socket, server_error, strlen(server_error);
				return 1;
			}

			//8. Determine content type in response header
			const char *content_type = "application/octet-stream" //Default
			char *file_extension = strrchr(actual_file_path, '.');
			if (file_extension != NULL && file_extension != actual_file_path){
				file_extension++;
				if (strcmp(file_extension, "html") == 0) content_type = "text/html";
				else if (strcmp(file_extension, "css") == 0) content_type = "text/css";
				else if (strcmp(file_extension, "js") == 0) content_type = "application/js";
				else if (strcmp(file_extension, "json") == 0) content_type = "application/json";
				else if (strcmp(file_extension, "pdf") == 0) content_type = "application/pdf";
				else if (strcmp(file_extension, "png") == 0) content_type = "image/png";
				else if (strcmp(file_extension, "jpg") == 0i || strcmp(file_extension, "jpeg") == 0) content_type = "image/jpeg";
			}

			//9. Build a Response header
			char header[1024];
			sprintf(header,
					"HTTP/1.1 200 OK\r\n"
					"Content-Type: text/html\r\n"
					"Content-Length: %ld\r\n"
					"Connection: close\r\n"
					"\r\n",
					file_size);

			//10. Send header then file content
			write(client_socket, header, strlen(header));
			write(client_socket, file_content, file_size);


			//11. Free the memory
			free(file_content);	
			free(full_path);
		}
		return 0;
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
			fprintf(stderr, "Read failed\n");
			continue;
		}
		else if (valread == 0)
		{
			printf("Empty request by client\n");
			close(client_socket);
			return 0;
		}
		else {
			buffer[valread] = '\0';
			printf("Received from client: %s\n", buffer);
		}

		//6. Parse request header
		HttpRequest client_request = {0};
		char *request_line_end = strstr(buffer, "\r\n");
		parse_client_request(buffer, &client_request, request_line_end);

		//Check if the get header name method works
		char *host = get_header_name(&client_request, "Host");
		char *localhostUrl = "127.0.0.1:4040";
		if (strcmp(host, localhostUrl) == 0) 
		{
			printf("The host is: %s\n", host);
			printf("LocalhostUrl is: %s\n", localhostUrl);
		}
		else
		{
			printf("Host: %s is not equal to localhost: %s\n", host, localhostUrl);
		}

		char *user_agent = get_header_name(&client_request, "User-Agent");
		if (user_agent)
		{
			printf("The user-agent is: %s\n", user_agent);
		}

		char *connection = get_header_name(&client_request, "Connection");
		if (connection)
		{
			printf("The connection is: %s\n", connection);
		}
		
		//Return method and protocol
		printf("Method: %s\n", client_request.method);
		printf("Protocol: %s\n", client_request.protocol);

		//Handle the method
		int handle_method(&client_request);

		//Return file if it exists
		char *file_root_directory = "files";
		char *request_path = client_request.path;

		char *path_buffer = malloc(strlen(file_root_directory)+strlen(request_path));
		snprintf(path_buffer, sizeof(path_buffer), "%s%s", file_root_directory, request_path);

		if(strcmp(path_buffer, "/")==0){
			snprintf(path_buffer, sizeof(path_buffer), "%s/index.html", file_root_directory);

		}
		
		char *file_name = strrchr(path_buffer, '/');
		if (file_name)
		{
			file_name++; 
			printf("Filename: %s\n", file_name);

			//7.Open the file

		}
		close(client_socket);
	}
	return 0;
}

