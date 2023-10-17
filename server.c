#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

// James Langille
// A01251664

// CONSTANTS
#define FIFO_OPTION "-fifo"
#define DOMAIN_OPTION "-domain"
#define FIFO 1
#define DOMAIN 2
#define LINE_LENGTH 4096
#define ARGS_LENGTH 10
#define FULL_PATH_LENGTH 256
#define BYTE_LENGTH 64

// FUNCTION HEADERS
int find_binary_executable(const char *command, char *full_path);
void read_from_fifo(const char *ipc_path, char *buffer);
void write_to_fifo(const char *ipc_path);
void execute_process(const char *full_path, char **args);
void split_input(const char *input, char **command, char **args,
                 int *args_used);
static void parse_arguments(char *argv[], int *ipc_option, char **ipc_path);
static void handle_arguments(int argc, const char *binary_name,
                             const int *ipc_option, const char *ipc_path);
static void read_from_socket(int *sockfd, const char *path, int *client_sockfd,
                             char *buffer);
static void write_to_socket(int sockfd);
static int socket_create(void);
static void socket_bind(int sockfd, const char *path);
static void start_listening(int server_fd, int backlog);
static int socket_accept_connection(int server_fd,
                                    struct sockaddr_un *client_addr);
static void handle_connection(int client_sockfd,
                              struct sockaddr_un *client_addr, char *buffer);
static void socket_close(int sockfd);
static void redirect_stdout(int fd);
_Noreturn static void usage(const char *program_name, int exit_code,
                            const char *message);

int main(int argc, char *argv[]) {
  char *args[ARGS_LENGTH];
  char buffer[LINE_LENGTH];
  char *command = NULL;
  char full_path[FULL_PATH_LENGTH];
  char *ipc_path;
  int args_used = 0;
  int ipc_option = 0;
  int sockfd;
  int client_sockfd;

  parse_arguments(argv, &ipc_option, &ipc_path);
  handle_arguments(argc, argv[0], &ipc_option, ipc_path);

  if (ipc_option == FIFO) {
    read_from_fifo(ipc_path, buffer);
  } else if (ipc_option == DOMAIN) {
    read_from_socket(&sockfd, ipc_path, &client_sockfd, buffer);
  }

  split_input(buffer, &command, args, &args_used);
  if (command == NULL) {
    fprintf(stderr, "Input can't be empty\n");
    exit(EXIT_FAILURE);
  }

  if (find_binary_executable(command, full_path) != 0) {
    free(command);
    for (int i = 0; i < args_used; i++) {
      if (args[i] != NULL) {
        free(args[i]);
      }
    }
    return EXIT_FAILURE;
  }

  if (ipc_option == 1) {
    write_to_fifo(ipc_path);
  } else if (ipc_option == 2) {
    write_to_socket(client_sockfd);
  }
  execute_process(full_path, args);

  free(command);
  for (int i = 0; i < args_used; i++) {
    if (args[i] != NULL) {
      free(args[i]);
    }
  }
  return EXIT_SUCCESS;
}

void split_input(const char *input, char **command, char **args,
                 int *args_used) {
  int args_count = 0;
  char *savePtr;
  const char delimiter[] = " ";
  char *token;
  char *input_copy = strdup(input);

  if (input_copy == NULL) {
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }
  token = strtok_r(input_copy, delimiter, &savePtr);

  while (token != NULL) {
    // Set command
    if (args_count == 0) {
      *command = (char *)malloc(strlen(token) + 1);

      if (*command == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
      }
      strcpy(*command, token);
    }

    // Set args
    args[args_count] = (char *)malloc(strlen(token) + 1);
    if (args[args_count] == NULL) {
      fprintf(stderr, "Memory allocation failed\n");
      exit(EXIT_FAILURE);
    }

    strcpy(args[args_count], token);
    token = strtok_r(NULL, delimiter, &savePtr);
    args_count++;
  }
  // execv requires for a null terminated list of args
  args[args_count] = NULL;
  *args_used = args_count;
  free(input_copy);
}

int find_binary_executable(const char *command, char *full_path) {
  char *savePtr;
  const char delimiter[] = ":";
  char *path = getenv("PATH");
  char *path_token;
  if (path == NULL) {
    fprintf(stderr, "PATH environment variable not found.\n");
    return EXIT_FAILURE;
  }

  path_token = strtok_r(path, delimiter, &savePtr);

  while (path_token != NULL) {
    snprintf(full_path, FULL_PATH_LENGTH, "%s/%s", path_token, command);
    // Checks if the path is an executable file
    if (access(full_path, X_OK) == 0) {
      // Binary executable found
      return EXIT_SUCCESS;
    }
    path_token = strtok_r(NULL, delimiter, &savePtr);
  }
  fprintf(stderr, "Command %s was not found.\n", command);
  return EXIT_FAILURE;
}

void execute_process(const char *full_path, char **args) {
  pid_t pid = fork();
  if (pid == -1) {
    perror("Error creating child process");
    exit(EXIT_FAILURE);
  } else if (pid == 0) {
    // Child process
    execv(full_path, args);
  } else {
    // Parent process
    int status;
    wait(&status);
    if (WIFEXITED(status)) {
      printf("Child process exited with status: %d\n", WEXITSTATUS(status));
    }
  }

  // Close redirected STD_OUT after fork
  close(STDOUT_FILENO);
}

void read_from_fifo(const char *ipc_path, char *buffer) {
  int fd;
  ssize_t bytes_read;

  if (ipc_path == NULL) {
    exit(EXIT_FAILURE);
  }

  mkfifo(ipc_path, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  fd = open(ipc_path, O_RDONLY | O_CLOEXEC);

  if (fd == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }

  bytes_read = read(fd, buffer, sizeof(char[BYTE_LENGTH]));

  if (bytes_read == -1) {
    perror("read");
    exit(EXIT_FAILURE);
  }

  buffer[bytes_read] = '\0';
  printf("Read %zd bytes from the FIFO\n", bytes_read);
  close(fd);
}

void write_to_fifo(const char *ipc_path) {
  int fd;

  if (ipc_path == NULL) {
    exit(EXIT_FAILURE);
  }
  fd = open(ipc_path, O_WRONLY | O_CLOEXEC);

  if (fd == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }

  redirect_stdout(fd);
  close(fd);
}

static int socket_create(void) {
  int sockfd;

#ifdef SOCK_CLOEXEC
  sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
  sockfd = socket(AF_UNIX, SOCK_STREAM, 0); // NOLINT(android-cloexec-socket)
#endif

  if (sockfd == -1) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  return sockfd;
}

static void socket_bind(int sockfd, const char *path) {
  struct sockaddr_un addr;

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
  addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bind");
    exit(EXIT_FAILURE);
  }

  printf("Bound to domain socket: %s\n", path);
}

static void start_listening(int server_fd, int backlog) {
  if (listen(server_fd, backlog) == -1) {
    perror("listen failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  printf("Listening for incoming connections...\n");
}

static int socket_accept_connection(int server_fd,
                                    struct sockaddr_un *client_addr) {
  int client_fd;
  char client_host[NI_MAXHOST];
  socklen_t client_addr_len;

  errno = 0;
  client_addr_len = sizeof(*client_addr);
  client_fd =
      accept(server_fd, (struct sockaddr *)client_addr, &client_addr_len);

  if (client_fd == -1) {
    if (errno != EINTR) {
      perror("accept failed");
    }

    return -1;
  }

  if (getnameinfo((struct sockaddr *)client_addr, client_addr_len, client_host,
                  NI_MAXHOST, NULL, 0, 0) == 0) {
    printf("Accepted a new connection from %s\n", client_host);
  } else {
    printf("Unable to get client information\n");
  }

  return client_fd;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void handle_connection(int client_sockfd,
                              struct sockaddr_un *client_addr, char *buffer) {
  ssize_t bytes_read;

  bytes_read = read(client_sockfd, buffer, sizeof(char[BYTE_LENGTH]));

  if (bytes_read == -1) {
    perror("read");
    exit(EXIT_FAILURE);
  }

  buffer[bytes_read] = '\0';
  printf("Read %zd bytes from the FIFO\nInput: %s\n", bytes_read, buffer);
}

#pragma GCC diagnostic pop

static void socket_close(int sockfd) {
  if (close(sockfd) == -1) {
    perror("Error closing socket");
    exit(EXIT_FAILURE);
  }
}

static void read_from_socket(int *sockfd, const char *path, int *client_sockfd,
                             char *buffer) {
  struct sockaddr_un client_addr;

  unlink(path);
  *sockfd = socket_create();
  socket_bind(*sockfd, path);
  start_listening(*sockfd, SOMAXCONN);

  *client_sockfd = socket_accept_connection(*sockfd, &client_addr);
  if (*client_sockfd == -1) {
    perror("socket_accept_connection");
    socket_close(*sockfd);
    exit(EXIT_FAILURE);
  }

  handle_connection(*client_sockfd, &client_addr, buffer);
  socket_close(*sockfd);
}

static void write_to_socket(int client_sockfd) {
  redirect_stdout(client_sockfd);
}

static void parse_arguments(char *argv[], int *ipc_option, char **ipc_path) {

  char *path = argv[2];

  // Check if first arg is a ipc option
  if (strcmp(argv[1], FIFO_OPTION) == 0) {
    *ipc_option = 1;
  } else if (strcmp(argv[1], DOMAIN_OPTION) == 0) {
    *ipc_option = 2;
  } else {
    usage(argv[1], EXIT_FAILURE, "Arg can only be -fifo or -domain");
  }

  // Check if second arg is a ipc path
  if (path[0] == '.' && path[1] == '/') {
    *ipc_path = path;
  } else {
    usage(argv[2], EXIT_FAILURE, "File path format starts with ./");
  }
}

static void handle_arguments(int argc, const char *binary_name,
                             const int *ipc_option, const char *ipc_path) {
  if (argc != 3) {
    usage(binary_name, EXIT_FAILURE, NULL);
  }

  if (*ipc_option == 0) {
    usage(binary_name, EXIT_FAILURE, "The ipc option is required.");
  }

  if (ipc_path == NULL) {
    usage(binary_name, EXIT_FAILURE, "The ipc path is required.");
  }
}

static void redirect_stdout(int fd) {
  if (dup2(fd, STDOUT_FILENO) == -1) {
    perror("dup2");
    close(fd);
    exit(EXIT_FAILURE);
  }
}

_Noreturn static void usage(const char *program_name, int exit_code,
                            const char *message) {
  if (message) {
    fprintf(stderr, "%s\n", message);
  }

  fprintf(stderr, "Usage: %s [-ipc option] <file path>\n", program_name);
  fputs("Options:\n", stderr);
  fputs("  -h  Display this help message\n", stderr);
  exit(exit_code);
}
