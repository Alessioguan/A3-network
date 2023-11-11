#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <fcntl.h>
#include <map>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>


#define SHELL_PORT 9002
#define FILE_PORT 9001
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10
char last_command_output[BUFFER_SIZE] = {0};
;
// Default values for the command line arguments
static int file_server_port = FILE_PORT;
static int shell_server_port = SHELL_PORT;
static int max_threads = 5;
static bool debug_mode = false;
static bool verbose_flag = false;

//general variable which need to lock are in this struct
struct FileAccessControl {
    int active_readers;
    int waiting_writers;
    bool writer_active;
    pthread_mutex_t access_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t readers_ok = PTHREAD_COND_INITIALIZER;
    pthread_cond_t writer_ok = PTHREAD_COND_INITIALIZER;
};

// Map to keep track of each file's access control
std::map<int, FileAccessControl> file_access_controls;
// debug operation
void debug_start_operation(const std::string& operation, int identifier, bool debug_mode) {
    if (debug_mode) {
        std::cout << "Starting " << operation << " operation on file identifier " << identifier << std::endl;
        if (operation == "read") {
            sleep(3);
        } else if (operation == "write") {
            sleep(6);
        }
    }
}

void debug_end_operation(const std::string& operation, int identifier, bool debug_mode) {
    if (debug_mode) {
        std::cout << "Ending " << operation << " operation on file identifier " << identifier << std::endl;
    }
}
void begin_read(int identifier) {
    pthread_mutex_lock(&file_access_controls[identifier].access_mutex);
    file_access_controls[identifier].active_readers++;
    while (file_access_controls[identifier].writer_active || file_access_controls[identifier].waiting_writers > 0) {
        pthread_cond_wait(&file_access_controls[identifier].readers_ok, &file_access_controls[identifier].access_mutex);
    }
    file_access_controls[identifier].active_readers--;
    pthread_mutex_unlock(&file_access_controls[identifier].access_mutex);
}

void end_read(int identifier) {
    pthread_mutex_lock(&file_access_controls[identifier].access_mutex);

    if (file_access_controls[identifier].active_readers == 0 && file_access_controls[identifier].waiting_writers > 0) {
        pthread_cond_signal(&file_access_controls[identifier].writer_ok);
    }

    pthread_mutex_unlock(&file_access_controls[identifier].access_mutex);
}

void begin_write(int identifier) {
    pthread_mutex_lock(&file_access_controls[identifier].access_mutex);
    file_access_controls[identifier].waiting_writers++;
    while (file_access_controls[identifier].writer_active || file_access_controls[identifier].active_readers > 0) {
        pthread_cond_wait(&file_access_controls[identifier].writer_ok, &file_access_controls[identifier].access_mutex);
    }
    file_access_controls[identifier].waiting_writers--;
    file_access_controls[identifier].writer_active = true;
    pthread_mutex_unlock(&file_access_controls[identifier].access_mutex);
}

void end_write(int identifier) {
    pthread_mutex_lock(&file_access_controls[identifier].access_mutex);
    file_access_controls[identifier].writer_active = false;

    if (file_access_controls[identifier].waiting_writers > 0) {
        pthread_cond_signal(&file_access_controls[identifier].writer_ok);
    } else {
        pthread_cond_broadcast(&file_access_controls[identifier].readers_ok);
    }

    pthread_mutex_unlock(&file_access_controls[identifier].access_mutex);
}


// Map from file identifier to file descriptor
std::map<int, int> open_files;
static std::map<std::string, int> file_to_identifier_map;
static std::map<int, std::string> identifier_to_file_map;

void *handle_shell_client(void *socket_desc) {
    int new_socket = (int) (intptr_t) socket_desc;
    char buffer[BUFFER_SIZE] = {0};
    bool connection_open = true;
    int status;
    char response[BUFFER_SIZE];
    char cmd_output[BUFFER_SIZE];
    char temp_output[BUFFER_SIZE * 10] = {0};  // Temporary buffer to accumulate the command output
    FILE *cmd;

    while (connection_open) {
        memset(buffer, 0, BUFFER_SIZE);
        // Read data sent by the client
        read(new_socket, buffer, BUFFER_SIZE);



        // Check if the client command is CPRINT
        if (strcmp(buffer, "CPRINT") == 0) {
            if(strcmp(response, "") == 0) {
                cmd = popen("", "r");
                sprintf(response, "ERR %d Command execution failed with exit status", status);
                send(new_socket, response, strlen(response), 0);
                status = pclose(cmd);
                continue;
            }else {
                send(new_socket, last_command_output, strlen(last_command_output), 0);
                continue;
            }
        }

        std::cout << "Received command: " << buffer << std::endl;
        cmd_output[BUFFER_SIZE] = {0};
        cmd = popen(buffer, "r");




        // Execute only when there is a command
        if (strcmp(buffer, "") == 0) {
            sprintf(response, "ERR %d Command execution failed with exit status", status);
            send(new_socket, response, strlen(response), 0);
            status = pclose(cmd);
            continue;
        }
        else {
            // Execute the command and return the result



            if (cmd == NULL) {
                sprintf(response, "FAIL %d Error in command execution", errno);
                send(new_socket, response, strlen(response), 0);
                status = pclose(cmd);
            } else {
                // Save the command output
                temp_output[BUFFER_SIZE * 10] = {0};
                while (fgets(cmd_output, sizeof(cmd_output), cmd) != NULL) {
                    strcat(temp_output, cmd_output);//concentrate temp_output and cmd_output to destination temp_output
                }
                strncpy(last_command_output, temp_output, sizeof(last_command_output) - 1);
                // output the cmd result
                std::cout << "Command output: " << temp_output << std::endl;
                if (status == 0) {
                    sprintf(response, "OK 0 Command executed successfully");
                } else {
                    sprintf(response, "ERR %d Command execution failed with exit status", status);
                }
                send(new_socket, response, strlen(response), 0);
                status = pclose(cmd);
            }
        }
    }
    close(new_socket);
    return NULL;
}

void *handle_file_client(void *socket_desc) {
    static int file_identifier_counter = 0;

    int new_socket = (int) (intptr_t) socket_desc;
    char buffer[BUFFER_SIZE] = {0};
    char response[BUFFER_SIZE] = {0};
    bool connection_open = true;

    while (connection_open) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t valread = read(new_socket, buffer, BUFFER_SIZE);
        std::cout << "Received file operation request: " << buffer << std::endl;
        // Tokenize the request
        char *token = strtok(buffer, " ");
        if (strcmp(token, "FOPEN") == 0) {
            token = strtok(NULL, " ");
            std::string filename(token);
            // Check if the file is already opened
            if (file_to_identifier_map.find(filename) != file_to_identifier_map.end()) {
                int existing_identifier = file_to_identifier_map[filename];
                sprintf(response, "ERR %d File already opened with identifier %d", ENOENT, existing_identifier);
            } else {
                int fd = open(token, O_RDWR | O_CREAT, 0666);
                if (fd < 0) {
                    sprintf(response, "FAIL %d Unable to open file", errno);
                } else {
                    int identifier = ++file_identifier_counter;
                    open_files[identifier] = fd;//The most convenient approach is for this integer to be the index of the file in the server’s descriptor table;
                    file_to_identifier_map[filename] = identifier;
                    identifier_to_file_map[identifier] = filename;
                    sprintf(response, "OK %d File opened successfully", identifier);
                }
            }
        } else if (strcmp(token, "FSEEK") == 0) {
            int identifier = atoi(strtok(NULL, " "));
            int offset = atoi(strtok(NULL, " "));
            if (open_files.find(identifier) == open_files.end()) {
                sprintf(response, "ERR ENOENT No such file");
            } else {
                lseek(open_files[identifier], offset, SEEK_CUR);
                sprintf(response, "OK 0 Seek successful");
            }
        } else if (strcmp(token, "FREAD") == 0) {
            int identifier = atoi(strtok(NULL, " "));
            int length = atoi(strtok(NULL, " "));
            if (open_files.find(identifier) == open_files.end()) {
                sprintf(response, "ERR ENOENT No such file");
            } else {
                char read_buffer[length + 1];
                debug_start_operation("read", identifier, debug_mode);
                begin_read(identifier);
                ssize_t bytes_read = read(open_files[identifier], read_buffer, length);
                sprintf(response, "OK %ld %s", bytes_read, read_buffer);
                end_read(identifier);
                debug_end_operation("read", identifier, debug_mode);
                if (bytes_read >= 0) {
                    sprintf(response, "OK %ld %s", bytes_read, read_buffer);
//                    sprintf(response, "OK %ld Read successful", bytes_read);
                } else {
                    sprintf(response, "ERR %d Read failed", errno);
                }
            }
        } else if (strcmp(token, "FWRITE") == 0) {
            int identifier = atoi(strtok(NULL, " "));
            char *bytes = strtok(NULL, " ");
            if (open_files.find(identifier) == open_files.end()) {
                sprintf(response, "ERR ENOENT No such file");
            } else {
                debug_start_operation("write", identifier, debug_mode);
                begin_write(identifier);
                ssize_t bytes_write = write(open_files[identifier], bytes, strlen(bytes));
                end_write(identifier);
                debug_end_operation("write", identifier, debug_mode);
                if (bytes_write >= 0) {
                    sprintf(response, "OK %ld Write successful", bytes_write);
                } else {
                    sprintf(response, "ERR %d Write failed", errno);
                }
            }
        } else if (strcmp(token, "FCLOSE") == 0) {
            int identifier = atoi(strtok(NULL, " "));
            if (open_files.find(identifier) == open_files.end()) {
                sprintf(response, "ERR ENOENT No such file");
            } else {
                close(open_files[identifier]);
                open_files.erase(identifier);
                // get the filename and remove it from the maps
                std::string filename = identifier_to_file_map[identifier];
                file_to_identifier_map.erase(filename);
                identifier_to_file_map.erase(identifier);
                sprintf(response, "OK 0 File closed successfully");
            }
        } else if (strcmp(buffer, "EXIT") == 0) {
            connection_open = false;
        } else {
            sprintf(response, "FAIL Unknown command");
        }
        send(new_socket, response, strlen(response), 0);
    }

    close(new_socket);
    return NULL;
}



int main(int argc, char *argv[]) {
// Parse the command line arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            file_server_port = atoi(argv[++i]);
            std::cout << "File server port: " << file_server_port << std::endl;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            shell_server_port = atoi(argv[++i]);
            std::cout << "Shell server port: " << shell_server_port << std::endl;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            max_threads = atoi(argv[++i]);
            std::cout << "Max threads: " << max_threads << std::endl;
        } else if (strcmp(argv[i], "-D") == 0) {
            debug_mode = true;
            std::cout << "Delay flag set" << std::endl;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose_flag = true;
            std::cout << "Verbose flag set" << std::endl;
        } else if (strcmp(argv[i], "echo") == 0)
        {
            const char* idCommand = "id -u";
            FILE* idFile = popen(idCommand, "r");

            if (!idFile) {
                std::cerr << "Error getting user ID." << std::endl;
                return 1;
            }

            char userIdBuffer[16];  // Assuming the user ID won't be larger than 16 characters
            if (!fgets(userIdBuffer, sizeof(userIdBuffer), idFile)) {
                std::cerr << "Error reading user ID." << std::endl;
                return 1;
            }

            // Close the file stream
            pclose(idFile);

            // Convert the user ID from string to integer
            int userId = std::atoi(userIdBuffer);

            // Calculate unique port numbers based on the user ID
            file_server_port = 8000 + userId;
            std::cout << "File server port: " << file_server_port << std::endl;
            shell_server_port = 9000 + userId;
            std::cout << "Shell server port: " << shell_server_port << std::endl;
        }

    }

    int shell_server_fd, file_server_fd, new_socket, max_sd, sd, activity;
    struct sockaddr_in address;
    int opt = 1;
    fd_set readfds;


    shell_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(shell_server_fd == -1)
        std::cerr << ("socket() error") << std::endl;
    file_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if( file_server_fd == -1)
        std::cerr << ("socket() error") << std::endl;

    // set shell server socket to allow multiple connections
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(shell_server_port);
    if(bind(shell_server_fd, (struct sockaddr *) &address, sizeof(address))==-1)
        std::cerr << ("bind() error") << std::endl;
    if(listen(shell_server_fd, max_threads) == -1)
        std::cerr << ("listen() error") << std::endl;

    address.sin_port = htons(file_server_port);
    if(bind(file_server_fd, (struct sockaddr *) &address, sizeof(address))==-1)
        std::cerr << ("bind() error") << std::endl;
    if(listen(file_server_fd, max_threads) == -1)
        std::cerr << ("listen() error") << std::endl;

    std::cout << "Listening on ports " << shell_server_port << " and " << file_server_port << std::endl;

    // Use select to multiplex between the two servers
    while (true) {
        FD_ZERO(&readfds);
        FD_SET(shell_server_fd, &readfds);
        FD_SET(file_server_fd, &readfds);
        max_sd = std::max(shell_server_fd, file_server_fd);

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        //FD_ISSET true when readfds include the information of fd
        if (FD_ISSET(shell_server_fd, &readfds)) {
            int client_shell_socket = accept(shell_server_fd, NULL, NULL);
            if (client_shell_socket == -1)
                std::cerr << ("accept() error") << std::endl;
            pthread_t shell_thread;
            pthread_create(&shell_thread, NULL, &handle_shell_client, (void *) (intptr_t) client_shell_socket);
            pthread_detach(shell_thread);
        }

        if (FD_ISSET(file_server_fd, &readfds)) {
            int client_file_socket = accept(file_server_fd, NULL, NULL);
            if (client_file_socket == -1)
                std::cerr << ("accept() error") << std::endl;
            pthread_t file_thread;
            pthread_create(&file_thread, NULL, &handle_file_client, (void *) (intptr_t) client_file_socket);
            pthread_detach(file_thread);
        }
    }
    close(shell_server_fd);
    close(file_server_fd);
    return 0;
}

