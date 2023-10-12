#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <regex.h>

#define MAX_CLIENTS 100
#define MAX_NICKNAME_LENGTH 12
#define MAX_MESSAGE_LENGTH 255
#define BUFFER_SIZE 1024

struct ClientInfo
{
  int socketfd = -1;
  bool verified = false;
  char nickname[MAX_NICKNAME_LENGTH + 1];
};

const char *SUPPORTED_PROTOCOL = "HELLO 1\n";
const char *ALLOWED_NAME = "OK\n";
const char *DENIED_NAME = "ERR Invalid name!\n";
const char *DENIED_MESSAGE = "ERROR Invalid message!\n";

bool isValidName(const char *name)
{
  regex_t regex;

  if (regcomp(&regex, "^[A-Za-z0-9_]{1,12}$", REG_EXTENDED) != 0)
  {
    printf("[ERROR] Error compiling regex expression!\n");
    fflush(stdout);
  }
  int result = regexec(&regex, name, 0, NULL, 0);
  regfree(&regex);

  return result == 0;
}

void removeClient(ClientInfo &client)
{
  close(client.socketfd);
  client.socketfd = -1;
  client.verified = false;
}

int main(int argc, char *argv[])
{
  char delim[] = ":";
  char *destHost = strtok(argv[1], delim);
  char *destPort = strtok(NULL, delim);

  if (destHost == NULL || destPort == NULL)
  {
    printf("[ERROR] Invalid input! Please use <DNS|IPv4|IPv6>:<port>\n");
    fflush(stdout);
    exit(EXIT_FAILURE);
  }
  printf("Host %s, and port %s\n", destHost, destPort);

  addrinfo hints;
  addrinfo *rp;
  addrinfo *result = NULL;

  memset(&hints, 0, sizeof(addrinfo));
  hints.ai_family = AF_UNSPEC; // (IPv4 or IPv6)
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(destHost, destPort, &hints, &result) != 0)
  {
    printf("[ERROR] Failed to get address info!\n");
    fflush(stdout);
    exit(EXIT_FAILURE);
  }
  int socketfd = -1;

  // Reference: https://linux.die.net/man/3/getaddrinfo
  for (rp = result; rp != NULL; rp = rp->ai_next)
  {
    socketfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (socketfd == -1)
    {
      continue;
    }

    if (bind(socketfd, rp->ai_addr, rp->ai_addrlen) == 0)
    {
      break; // Success
    }
    close(socketfd);
  }

  // No address succeeded
  if (rp == NULL)
  {
    printf("[ERROR] Failed to bind!\n");
    fflush(stdout);
    exit(EXIT_FAILURE);
  }
  freeaddrinfo(result);

  if (listen(socketfd, MAX_CLIENTS) < 0)
  {
    printf("[ERROR] Failed to listen!\n");
    fflush(stdout);
    close(socketfd);
    exit(EXIT_FAILURE);
  }
  printf("Listening for incoming connections...\n");

  bool running = true;
  fd_set readfds;
  int maxSocket;
  sockaddr_in serverAddr;
  int flags = 0;

  ClientInfo clients[MAX_CLIENTS];
  char buffer[BUFFER_SIZE];

  while (running)
  {
    // Clear
    FD_ZERO(&readfds);
    memset(buffer, 0, BUFFER_SIZE);

    // Add server socket to read list
    FD_SET(socketfd, &readfds);
    maxSocket = socketfd;

    // Add clients to our read list
    for (auto &client : clients)
    {
      int clientSocket = client.socketfd;

      // If the socket is valid then we add it to the read list
      if (clientSocket > 0)
      {
        FD_SET(clientSocket, &readfds);
      }

      // Highest file descriptor number as we need it for the select function
      if (clientSocket > maxSocket)
      {
        maxSocket = clientSocket;
      }
    }

    // Wait for activity on one of the sockets
    if (select(maxSocket + 1, &readfds, NULL, NULL, NULL) < 0)
    {
      printf("[ERROR] Failed to select!\n");
      fflush(stdout);
      close(socketfd);
      exit(EXIT_FAILURE);
    }

    // We check for activity on main server socket and if so we have a new connection
    if (FD_ISSET(socketfd, &readfds))
    {
      int clientSocket;
      socklen_t addrlen = sizeof(serverAddr);

      if ((clientSocket = accept(socketfd, (sockaddr *)&serverAddr, (socklen_t *)&addrlen)) < 0)
      {
        printf("[ERROR] Failed to accept socket!\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
      }
      printf("Client connected from %s:%d\n", inet_ntoa(serverAddr.sin_addr), ntohs(serverAddr.sin_port));
      printf("Server protocol: %s", SUPPORTED_PROTOCOL);
      fflush(stdout);

      // Send protocol info to client
      if (send(clientSocket, SUPPORTED_PROTOCOL, strlen(SUPPORTED_PROTOCOL), flags) < 0)
      {
        printf("[ERROR] Failed to send the protocol to the client!\n");
        fflush(stdout);
        close(clientSocket);
        continue; // Since we failed, do not store the client!
      }
      bool success = false;

      for (auto &client : clients)
      {
        if (client.socketfd == -1)
        {
          client.socketfd = clientSocket;
          success = true;
          break;
        }
      }

      if (!success)
      {
        printf("[ERROR] Could not store client! No space left?\n");
        fflush(stdout);
        close(clientSocket);
      }
    }
    else
    {
      // In this case we some activity on one of the client sockets
      for (auto &client : clients)
      {
        int clientSocket = client.socketfd;

        // Check for activity on this specific client
        if (FD_ISSET(clientSocket, &readfds))
        {
          size_t bytesRead = recv(clientSocket, buffer, BUFFER_SIZE, flags);

          // Check status
          if (bytesRead == 0) // Disconnected
          {
            printf("Client left!\n");
            fflush(stdout);
            removeClient(client);
          }
          else if (bytesRead < 0) // Errors
          {
            printf("[ERROR] Could not read data from socket!\n");
            fflush(stdout);
            removeClient(client);
          }
          else // OK
          {
            buffer[bytesRead] = '\0';

            // Check if we have verified the nickname of the client or not
            if (!client.verified)
            {
              char clientName[MAX_NICKNAME_LENGTH];

              // Check so the message contains "NICK <name>", follows name-format (A-Za-z0-9\_) and max length of name is 12
              if (sscanf(buffer, "NICK %s\n", clientName) == 1 && isValidName(clientName) && strlen(buffer) <= MAX_NICKNAME_LENGTH + 6)
              {
                printf("Name is allowed!\n");
                fflush(stdout);
                client.verified = true;
                // Set nickname
                strncpy(client.nickname, clientName, MAX_NICKNAME_LENGTH);
                client.nickname[MAX_NICKNAME_LENGTH] = '\0';

                if (send(clientSocket, ALLOWED_NAME, strlen(ALLOWED_NAME), flags) < 0)
                {
                  printf("Failed to accept-nickname message to client!\n");
                  fflush(stdout);
                  removeClient(client);
                }
              }
              else
              {
                if (send(clientSocket, DENIED_NAME, strlen(DENIED_NAME), flags) < 0)
                {
                  printf("Failed to send denied-nickname message to client!\n");
                  fflush(stdout);
                  removeClient(client);
                }
              }
            }
            else
            {
              // Incoming message from a client
              char message[MAX_MESSAGE_LENGTH];

              // Make sure the data starts is MSG-format and read the message
              if (sscanf(buffer, "MSG %[^\n]", message) == 1)
              {
                char echoMessage[MAX_MESSAGE_LENGTH + MAX_NICKNAME_LENGTH + 6];
                snprintf(echoMessage, sizeof(echoMessage), "MSG %s %s\n", client.nickname, message);

                // Echo message to all clients
                for (auto &client : clients)
                {
                  if (client.socketfd != -1)
                  {
                    if (send(client.socketfd, echoMessage, strlen(echoMessage), flags) < 0)
                    {
                      printf("[ERROR] Failed to send echo message to a client!\n");
                      fflush(stdout);
                      removeClient(client);
                    }
                  }
                }
              }
              else
              {
                if (send(clientSocket, DENIED_MESSAGE, strlen(DENIED_NAME), flags) < 0)
                {
                  printf("[ERROR] Failed to send deny-message to client!\n");
                  fflush(stdout);
                  removeClient(client);
                }
              }
            }
          }
        }
      }
    }
  }
}
