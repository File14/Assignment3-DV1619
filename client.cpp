#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#define MAX_NICKNAME_LENGTH 12
#define MAX_MESSAGE_LENGTH 255
#define BUFFER_SIZE 1024

const char *SUPPORTED_PROTOCOL = "HELLO 1\n";
const char *ALLOWED_NAME = "OK\n";

int main(int argc, char *argv[])
{
  if (argc != 3)
  {
    printf("[ERROR] Invalid input! Please use <DNS|IPv4|IPv6>:<port> <nickname>\n");
    fflush(stdout);
    exit(EXIT_FAILURE);
  }
  char delim[] = ":";
  char *destHost = strtok(argv[1], delim);
  char *destPort = strtok(NULL, delim);

  if (destHost == NULL || destPort == NULL)
  {
    printf("[ERROR] Invalid input! Please use <DNS|IPv4|IPv6>:<port> <nickname>\n");
    fflush(stdout);
    exit(EXIT_FAILURE);
  }
  printf("Host %s, and port %s\n", destHost, destPort);

  addrinfo hints;
  addrinfo *result = NULL;
  addrinfo *res;

  // Allocate memory for addrinfo
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC; // (IPv4 or IPv6)
  hints.ai_socktype = SOCK_STREAM;

  // Reference: https://linux.die.net/man/3/getaddrinfo
  if (getaddrinfo(destHost, destPort, &hints, &result) != 0)
  {
    printf("[ERROR] Failed to resolve host!\n");
    fflush(stdout);
    exit(EXIT_FAILURE);
  }
  int socketfd = -1;

  for (res = result; res != NULL; res = res->ai_next)
  {
    socketfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if (socketfd == -1)
    {
      continue;
    }

    if (connect(socketfd, res->ai_addr, res->ai_addrlen) != -1)
    {
      printf("Connected to %s:%s\n", destHost, destPort);
      fflush(stdout);
      break;
    }
    close(socketfd);
  }

  if (res == NULL)
  {
    printf("[ERROR] Failed to connect to the host!\n");
    fflush(stdout);
    exit(EXIT_FAILURE);
  }
  freeaddrinfo(result);

  char buffer[BUFFER_SIZE];
  memset(buffer, 0, BUFFER_SIZE);
  int flags = 0;
  // Read the incoming protocol message
  if (recv(socketfd, buffer, BUFFER_SIZE, flags) <= 0)
  {
    printf("[ERROR] Failed to get response from server!\n");
    fflush(stdout);
    close(socketfd);
    exit(EXIT_FAILURE);
  }
  printf("Server protocol: %s", buffer);

  if (strcmp(buffer, SUPPORTED_PROTOCOL) != 0)
  {
    printf("[ERROR] Unsupported protocol!\n");
    fflush(stdout);
    close(socketfd);
    exit(EXIT_FAILURE);
  }
  printf("Protocol supported, sending nickname\n");

  // Reset buffer
  memset(buffer, 0, BUFFER_SIZE);

  char nickname[MAX_NICKNAME_LENGTH];
  strncpy(nickname, argv[2], sizeof(nickname) - 1);

  // Send the nickname to the server
  char nickMessage[MAX_NICKNAME_LENGTH + 6];
  snprintf(nickMessage, sizeof(nickMessage), "NICK %s\n", nickname);

  if (send(socketfd, nickMessage, strlen(nickMessage), flags) < 0)
  {
    printf("[ERROR] Failed to send the nickname to server!\n");
    fflush(stdout);
    close(socketfd);
    exit(EXIT_FAILURE);
  }
  // Wait for the server to respond!
  if (recv(socketfd, buffer, BUFFER_SIZE, flags) <= 0)
  {
    printf("[ERROR] Failed to get response from server!\n");
    fflush(stdout);
    close(socketfd);
    exit(EXIT_FAILURE);
  }

  if (strcmp(buffer, ALLOWED_NAME) != 0)
  {
    printf("[ERROR] Name was not accepted!\n");
    fflush(stdout);
    close(socketfd);
    exit(EXIT_FAILURE);
  }
  printf("Name accepted!\n");
  fflush(stdout);

  fd_set readFds;
  bool running = true;

  while (running)
  {
    FD_ZERO(&readFds);
    FD_SET(STDIN_FILENO, &readFds);
    FD_SET(socketfd, &readFds);

    int maxfd = (STDIN_FILENO > socketfd) ? STDIN_FILENO : socketfd;

    // Wait for activity on either STDIN (user input) or the socket
    if (select(maxfd + 1, &readFds, NULL, NULL, NULL) < 0)
    {
      printf("[ERROR] Failed to select!\n");
      fflush(stdout);
      close(socketfd);
      exit(EXIT_FAILURE);
    }

    // Check if there is input from STDIN
    if (FD_ISSET(STDIN_FILENO, &readFds))
    {
      char input[MAX_MESSAGE_LENGTH];
      fgets(input, sizeof(input), stdin); // new line is included

      // Now we format the message to "MSG <text>"
      char formattedMessage[MAX_MESSAGE_LENGTH + MAX_MESSAGE_LENGTH + 4];
      snprintf(formattedMessage, sizeof(formattedMessage), "MSG %s", input);

      // We send the formatted message the the server
      if (send(socketfd, formattedMessage, sizeof(formattedMessage), flags) < 0)
      {
        printf("[ERROR] Failed to send chat message to server!\n");
        fflush(stdout);
        running = false;
        close(socketfd);
        exit(EXIT_FAILURE);
      }
    }

    // Check if there is data from the server
    if (FD_ISSET(socketfd, &readFds))
    {
      memset(buffer, 0, BUFFER_SIZE);

      if (recv(socketfd, buffer, BUFFER_SIZE, flags) <= 0)
      {
        printf("[ERROR] Failed to get response from server!\n");
        fflush(stdout);
        running = false;
        close(socketfd);
        exit(EXIT_FAILURE);
      }
      char name[MAX_NICKNAME_LENGTH];
      char message[MAX_MESSAGE_LENGTH];

      if (sscanf(buffer, "MSG %12s %[^\n]", name, message) == 2)
      {
        // Make sure name is not the as this client
        if (strcmp(name, nickname) != 0)
        {
          printf("%s: %s\n", name, message);
          fflush(stdout);
        }
      }
      else
      {
        printf("[ERROR] Failed to parse the echo-message!\n");
        fflush(stdout);
      }
    }
  }
  close(socketfd);
  return 0;
}
