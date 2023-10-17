#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// James Langille
// A01251664

// CONSTANTS
#define FIFO_OPTION "-fifo"
#define DOMAIN_OPTION "-domain"
#define FIFO 1
#define DOMAIN 2
#define LINE_LENGTH 1024

// FUNCTION HEADERS
static void parse_arguments(char *argv[], int *ipc_option, char **ipc_path);
static void handle_arguments(int argc, const char *binary_name,
                             const int *ipc_option, const char *ipc_path,
                             const char *command);
void write_to_fifo(const char *ipc_path, const char *command);
void read_from_fifo(const char *ipc_path);
static int socket_create(void);
static void setup_socket_address(struct sockaddr_un *addr, const char *path);
static int connect_to_server(int sockfd, const struct sockaddr_un *addr);
void write_to_socket(int sockfd, const char *command);
static void read_from_socket(int sockfd);
static void socket_close(int sockfd);
_Noreturn static void usage(const char *program_name, int exit_code,
                            const char *message);

int main(int argc, char *argv[]) {
  int ipc_option = 0;
  char *ipc_path = NULL;
  struct sockaddr_un addr;

  parse_arguments(argv, &ipc_option, &ipc_path);
  handle_arguments(argc, argv[0], &ipc_option, ipc_path, argv[3]);

  if (ipc_option == FIFO) {
    write_to_fifo(ipc_path, argv[3]);
    read_from_fifo(ipc_path);
  }

  if (ipc_option == DOMAIN) {
    int sockfd;
    setup_socket_address(&addr, ipc_path);
    sockfd = socket_create();
    connect_to_server(sockfd, &addr);
    write_to_socket(sockfd, argv[3]);
    read_from_socket(sockfd);
    unlink(ipc_path);
  }

  return EXIT_SUCCESS;
}

void write_to_fifo(const char *ipc_path, const char *command) {
  int fd;
  ssize_t bytes_written;

  if (ipc_path == NULL) {
    exit(EXIT_FAILURE);
  }
  fd = open(ipc_path, O_WRONLY | O_CLOEXEC);

  if (fd == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }

  bytes_written = write(fd, command, strlen(command));

  if (bytes_written == -1) {
    perror("write");
    exit(EXIT_FAILURE);
  }
  close(fd);
}

void read_from_fifo(const char *ipc_path) {
  int fd;
  ssize_t bytes_read;
  char buffer[LINE_LENGTH];

  if (ipc_path == NULL) {
    exit(EXIT_FAILURE);
  }

  fd = open(ipc_path, O_RDONLY | O_CLOEXEC);

  if (fd == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }

  while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
    if (write(STDOUT_FILENO, buffer, (size_t)bytes_read) == -1) {
      perror("write");
      close(fd);
      exit(EXIT_FAILURE);
    }
  }
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

static void setup_socket_address(struct sockaddr_un *addr, const char *path) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  strncpy(addr->sun_path, path, sizeof(addr->sun_path) - 1);
  addr->sun_path[sizeof(addr->sun_path) - 1] = '\0';
}

static int connect_to_server(int sockfd, const struct sockaddr_un *addr) {
  if (connect(sockfd, (const struct sockaddr *)addr, sizeof(*addr)) == -1) {
    perror("Connection failed");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  return sockfd;
}

static void socket_close(int sockfd) {
  if (close(sockfd) == -1) {
    perror("Error closing socket");
    exit(EXIT_FAILURE);
  }
}

void write_to_socket(int sockfd, const char *command) {
  size_t size;
  size = strlen(command);
  printf("Command: %s\n", command);
  printf("Size: %zu\n", size);
  write(sockfd, command, size);
}

static void read_from_socket(int sockfd) {
  ssize_t bytes_read;
  char buffer[LINE_LENGTH];

  // Read from the socket
  while ((bytes_read = read(sockfd, buffer, sizeof(buffer))) > 0) {
    // Write to the terminal
    if (write(STDOUT_FILENO, buffer, (size_t)bytes_read) == -1) {
      perror("write");
      close(sockfd);
      exit(EXIT_FAILURE);
    }
  }
  socket_close(sockfd);
}

static void parse_arguments(char *argv[], int *ipc_option, char **ipc_path) {
  char *path;

  if (argv[2] != NULL) {
    path = argv[2];
  } else {
    usage(argv[2], EXIT_FAILURE, "File path can't be null");
  }

  // Check if first arg is a ipc option
  if (strcmp(argv[1], FIFO_OPTION) == 0) {
    *ipc_option = FIFO;
  } else if (strcmp(argv[1], DOMAIN_OPTION) == 0) {
    *ipc_option = DOMAIN;
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
                             const int *ipc_option, const char *ipc_path,
                             const char *command) {
  if (argc > 4) {
    usage(binary_name, EXIT_FAILURE, NULL);
  }

  if (ipc_option == 0) {
    usage(binary_name, EXIT_FAILURE, "The ipc option is required.");
  }

  if (ipc_path == NULL) {
    usage(binary_name, EXIT_FAILURE, "The ipc path is required.");
  }

  if (command == NULL) {
    usage(binary_name, EXIT_FAILURE, "Command can't be empty.");
  }
}

_Noreturn static void usage(const char *program_name, int exit_code,
                            const char *message) {
  if (message) {
    fprintf(stderr, "%s\n", message);
  }

  fprintf(stderr, "Usage: %s [-ipc option] <file path> \"command\"\n",
          program_name);
  fputs("Options:\n", stderr);
  fputs("  -h  Display this help message\n", stderr);
  exit(exit_code);
}
