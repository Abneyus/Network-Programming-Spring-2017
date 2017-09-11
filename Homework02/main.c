#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <signal.h>

volatile sig_atomic_t timedOut = 0;
volatile sig_atomic_t numTimes = 0;

void timeout_handler(int signal)
{
  timedOut = 1;
  numTimes += 1;
}

// Creates an ACK packet for the given blocknumbers.
char* genAckPacket(int blockNumber, int blockNumber2)
{
  char* toReturn = calloc(sizeof(char), 4);
  toReturn[1] = 4;
  toReturn[2] = blockNumber;
  toReturn[3] = blockNumber2;
  return toReturn;
}

// Creates an ERR packet with the given errorcode and message.
char* genErrPacket(int errorCode, char* message, int message_size)
{
  char* toReturn = calloc(sizeof(char), 5 + message_size);
  int a = 0;
  toReturn[1] = 5;
  toReturn[3] = errorCode;
  for(a = 0; a < message_size; a++) {
    toReturn[a+4] = message[a];
  }

  return toReturn;
}

// Creates a DATA packet given blocknumber and data to send.
char* genDataPacket(char blockNumber, char blockNumber2, char* buffer, int bytes_sent)
{
  char* toReturn = calloc(sizeof(char), 4 + bytes_sent);
  int a = 0;
  toReturn[1] = 3;
  toReturn[2] = blockNumber;
  toReturn[3] = blockNumber2;
  for(a = 0; a < bytes_sent; a++)
  {
    toReturn[a+4] = buffer[a];
  }
  return toReturn;
}

//Determines if the given packet is related to the given blocknumbers.
int recievedPacket(int blockNumber, int blockNumber2, char* ack_buffer)
{
  if(ack_buffer[2] == blockNumber && ack_buffer[3] == blockNumber2)
  {
    return 1;
  }

  return 0;
}

int main()
{
  int server_fd;
  struct sockaddr_in sock_server;
  socklen_t size;

  char buffer[516];

  int port;
  int stopNow = 0;
  int pid;

  server_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if(server_fd < 0)
  {
    fprintf(stderr, "socket() failure\n");
    return EXIT_FAILURE;
  }

  sock_server.sin_family = AF_INET;
  sock_server.sin_addr.s_addr = INADDR_ANY;
  sock_server.sin_port = htons(0);

  size = sizeof(sock_server);

  if(bind(server_fd, (struct sockaddr*)&sock_server, size) < 0)
  {
    fprintf(stderr, "bind() failure\n");
    return EXIT_FAILURE;
  }

  if(getsockname(server_fd, (struct sockaddr*)&sock_server, &size) < 0)
  {
    fprintf(stderr, "getsockname() failure\n");
    return EXIT_FAILURE;
  }

  port = ntohs(sock_server.sin_port);
  printf("%d\n", port);

  listen(server_fd, 1);

  while(!stopNow)
  {

    struct sockaddr client_sock;
    socklen_t client_size = sizeof(client_sock);

    //Recieves the initial RRQ or WRQ packet.
    int bytes_recieved = recvfrom(server_fd, buffer, 516, 0, &client_sock, &client_size);
    if(bytes_recieved < 0)
    {
      fprintf(stderr, "%s", strerror(errno));
      return EXIT_FAILURE;
    }

    #ifdef DEBUG_MODE
      printf("DEBUG: Caught something.\n");
    #endif

    //Client is connected
    pid = fork();

    if(pid == 0) {
      close(server_fd);

      signal(SIGALRM, timeout_handler);

      //Creating the new non-default server socket to recieve data on.
      int gen_fd;
      struct sockaddr_in gen_sock;
      socklen_t gen_size = sizeof(gen_sock);

      gen_sock.sin_family = AF_INET;
      gen_sock.sin_addr.s_addr = INADDR_ANY;
      gen_sock.sin_port = htons(0);

      gen_fd = socket(AF_INET, SOCK_DGRAM, 0);
      if(gen_fd < 0)
      {
        fprintf(stderr, "socket() failure\n");
        return EXIT_FAILURE;
      }

      if(bind(gen_fd, (struct sockaddr*)&gen_sock, gen_size) < 0)
      {
        fprintf(stderr, "bind() failure\n");
        return EXIT_FAILURE;
      }

      if(getsockname(gen_fd, (struct sockaddr*)&gen_sock, &gen_size) < 0)
      {
        fprintf(stderr, "getsockname() failure\n");
        return EXIT_FAILURE;
      }

      #ifdef DEBUG_MODE
        printf("DEBUG: Listening on %d\n", ntohs(gen_sock.sin_port));
      #endif

      listen(gen_fd, 1);

      //Pulls the filename from the RRQ/WRQ
      char fileName[516];
      strncpy(fileName, buffer+2, 516);

      //******** BEGIN RRQ ********
      if(1 & buffer[1])
      {
        #ifdef DEBUG_MODE
          printf("DEBUG: Recieved RRQ.\n");
          printf("DEBUG: fileName: %s\n", fileName);
        #endif

        int file_fd = open(fileName, O_RDONLY | O_CREAT, 0666);

        if(file_fd == -1)
        {
          char* p_err = genErrPacket(0, "Error Opening File", 18);
          sendto(gen_fd, p_err, sizeof(p_err), 0, &client_sock, client_size);
          free(p_err);
          close(gen_fd);
          printf("ERROR: open() failure for %s\n", fileName);
          return EXIT_FAILURE;
        }

        char blockNumber = 0;
        char blockNumber2 = 1;
        int bytes_sent = 0;

        int retry = 0;

        do
        {
          //Stops resending the packet, we've already resent it 10 times.
          if(timedOut && numTimes > 9)
          {
            #ifdef DEBUG_MODE
              printf("DEBUG: Timed out for the last time.\n");
            #endif
            close(file_fd);
            close(gen_fd);
            return EXIT_FAILURE;
          }
          //Resend the packet, haven't hit the 10 limit yet.
          else if (timedOut && numTimes < 10)
          {
            #ifdef DEBUG_MODE
              printf("DEBUG: Timed out %d\n", numTimes);
            #endif

            timedOut = 0;
            char* p_data = genDataPacket(blockNumber, blockNumber2, buffer, bytes_sent);

            sendto(gen_fd, p_data, sizeof(p_data), 0, &client_sock, client_size);

            free(p_data);
          }
          //Packet acknowledged successfully, send the next one.
          else if(!retry)
          {
            bytes_sent = read(file_fd, buffer, 512);

            char* p_data = genDataPacket(blockNumber, blockNumber2, buffer, bytes_sent);

            sendto(gen_fd, p_data, 4 + bytes_sent, 0, &client_sock, client_size);

            free(p_data);
          }
          retry = 0;

          char ack_buffer[4];

          alarm(1);
          bytes_recieved = recvfrom(gen_fd, ack_buffer, 4, 0, &client_sock, &client_size);

          //Increment block numbers if the package was recieved successfully.
          //Also resets the timeout counter.
          //Also accounts for sorcerer's apprentice, wont increment if we recieved
          //the wrong ACK.
          if(!timedOut && recievedPacket(blockNumber, blockNumber2, ack_buffer))
          {
            numTimes = 0;
            if(blockNumber2 + 1 == 0)
            {
              blockNumber += 1;
              blockNumber2 = 0;
            }
            else
            {
              blockNumber2 += 1;
            }
          }
          else if(!recievedPacket(blockNumber, blockNumber2, ack_buffer))
          {
            retry = 1;
          }
        }
        //Exits at end of file (under 512 bytes read).
        while(bytes_sent == 512 || timedOut || retry);
        close(file_fd);
      }
      //******** BEGIN WRQ ********
      else if(2 & buffer[2])
      {
        #ifdef DEBUG_MODE
          printf("DEBUG: Recieved WRQ.\n");
          printf("DEBUG: fileName: %s\n", fileName);
        #endif

        //Delete any existing file.
        FILE* delFile = fopen(fileName, "w");
        fclose(delFile);

        int file_fd = open(fileName, O_WRONLY | O_CREAT, 0666);

        if(file_fd == -1)
        {
          char* p_err = genErrPacket(0, "Error Creating File", 19);
          sendto(gen_fd, p_err, sizeof(p_err), 0, &client_sock, client_size);
          free(p_err);
          close(gen_fd);
          printf("open() failure\n");
          return EXIT_FAILURE;
        }

        //Create ACK packet for 0th block.
        char* p_ack = genAckPacket(0, 0);
        sendto(gen_fd, p_ack, sizeof(p_ack), 0, &client_sock, client_size);
        free(p_ack);

        p_ack = NULL;

        char blockNumber = 0;
        char blockNumber2 = 1;

        int retry = 0;

        do
        {
          retry = 0;
          alarm(1);
          bytes_recieved = recvfrom(gen_fd, buffer, 516, 0, &client_sock, &client_size);

          if(bytes_recieved < 0)
          {
            fprintf(stderr, "ERROR: recvfrom interrupted\n");
            close(gen_fd);
            close(file_fd);
            return EXIT_FAILURE;
          }

          if(timedOut && numTimes < 10)
          {
            #ifdef DEBUG_MODE
              printf("DEBUG: Timed out %d\n", numTimes);
            #endif
            timedOut = 0;
            sendto(gen_fd, p_ack, sizeof(p_ack), 0, &client_sock, client_size);
          }
          else if (timedOut && numTimes > 9)
          {
            #ifdef DEBUG_MODE
              printf("DEBUG: Timed out for the final time.\n");
            #endif
            close(gen_fd);
            close(file_fd);
            return EXIT_FAILURE;
          }
          else
          {
            numTimes = 0;

            if(buffer[2] == blockNumber && buffer[3] == blockNumber2)
            {
              write(file_fd, buffer+4, bytes_recieved-4);

              if(p_ack != NULL)
                free(p_ack);
              p_ack = genAckPacket(buffer[2], buffer[3]);
              sendto(gen_fd, p_ack, sizeof(p_ack), 0, &client_sock, client_size);

              //Expect the next packet.
              if(blockNumber2 + 1 == 0)
              {
                blockNumber+=1;
                blockNumber2=0;
              }
              else
              {
                blockNumber2+=1;
              }
            }
            // Sorcerer's Apprentice: If it's not the packet we're expecting,
            // ignore it and try to recieve the DATA again.
            else
            {
              retry = 1;
            }
          }
        }
        while (bytes_recieved == 516 || retry);

        close(file_fd);
      }
      close(gen_fd);
      return EXIT_SUCCESS;
    } else {

    }

  }

  close(server_fd);

  return EXIT_SUCCESS;
}
