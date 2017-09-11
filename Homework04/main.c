#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/select.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#if defined(__APPLE__)
  #define COMMON_DIGEST_FOR_OPENSSL
  #include <CommonCrypto/CommonDigest.h>
  #define SHA1 CC_SHA1
#else
  #include <openssl/md5.h>
#endif

#define BUFFER_SIZE 10240

int server(int port);
int client(int port);

int server_thread(int fd);
void client_thread(int fd);

bool c_query(int fd, char* filename);
void c_get(int fd, char* filename);
void c_put(int fd, char* filename);
int getArrayIndex(char** inHere, char* toFind, int max);
int c_contents(int fd, char*** files, char*** hashes);
int c_dircontents(char*** files, char*** hashes);
char* hashFile(char* fileName);

char* deHex(char* hash);

void reHash();

char* self;

// Deteremines whether or not this will function as a client or server and branches
int main(int argc, char** argv)
{
  int port = -1;
  bool isClient = 0;

  self = argv[0];

  if(argc != 3)
  {
    fprintf(stderr, "ERROR: Invalid number of arguments.\n");
    fprintf(stderr, "Expected [program] [client/server] [port]\n");
    return EXIT_FAILURE;
  }
  else
  {
    isClient = (strcmp(argv[1], "client") == 0);
    port = atoi(argv[2]);
  }

  #ifdef DEBUG_MODE
    printf("DEBUG: client\t%d port\t%d\n", isClient, port);
    fflush(stdout);
  #endif

  if(isClient)
  {
    return client(port);
  }
  else
  {
    return server(port);
  }
}

// Sets up the socket and connects to the server, then passes the fd to client_thread.
int client(int port)
{
  #ifdef DEBUG_MODE
    printf("DEBUG: Client started with port %d\n", port);
    fflush(stdout);
  #endif

  int tfd_server = socket(AF_INET, SOCK_STREAM, 0);
  if(tfd_server == -1)
  {
    fprintf(stderr, "ERROR: Error on TCP socket() call\n");
    return EXIT_FAILURE;
  }

  struct sockaddr_in tsock_server;
  socklen_t tsocklen_server = sizeof(tsock_server);
  tsock_server.sin_family = AF_INET;
  tsock_server.sin_addr.s_addr = INADDR_ANY;
  tsock_server.sin_port = htons(port);

  if(connect(tfd_server, (struct sockaddr*)&tsock_server, tsocklen_server) < 0)
  {
    fprintf(stderr, "ERROR: Error on TCP connect() call\n");
    return EXIT_FAILURE;
  }

  #ifdef DEBUG_MODE
    printf("DEBUG: Client connected successfully with port %d\n", port);
    fflush(stdout);
  #endif

  client_thread(tfd_server);

  return EXIT_SUCCESS;

}

// Gets the files available on the server and those available on the client.
// Iterates over both sets of files checking for mismatched hashes or missing
// files. Uploads/downloads as appropriate.
void client_thread(int fd)
{
  int a = 0;

  char** server_files = calloc(sizeof(char*), 1000);
  char** server_hashes = calloc(sizeof(char*), 1000);
  int server_tracked = c_contents(fd, &server_files, &server_hashes);

  char** client_files = calloc(sizeof(char*), 1000);
  char** client_hashes = calloc(sizeof(char*), 1000);
  int client_tracked = c_dircontents(&client_files, &client_hashes);

  #ifdef DEBUG_MODE
    printf("DEBUG: Checking against %d server files.\n", server_tracked);
    fflush(stdout);
  #endif

  // Iterates over the files that are tracked on the server. Attempts to find
  // the file on the client, if the file doesn't exist on the client, download
  // it. If the client does exist, check its hash against the server file hash.
  // If the hashes don't match, run the query method which returns whether the
  // client file should be uploaded or replaced.
  for(; a < server_tracked; a++)
  {
    int index = getArrayIndex(client_files, server_files[a], client_tracked);
    #ifdef DEBUG_MODE
      printf("DEBUG: file:\t%s index:\t%d\n", server_files[a], index);
      fflush(stdout);
    #endif
    if(index == -1)
    {
      // Download file from server, because it doesn't exist on client.
      printf("[client] Detected new file on server\n");
      c_get(fd, server_files[a]);
    }
    else
    {
      if(strncmp(client_hashes[index], server_hashes[a], 16) != 0)
      {
        bool todo = c_query(fd, client_files[index]);
        if(todo)
        {
          c_put(fd, client_files[index]);
        }
        else
        {
          printf("[client] Detected different & newer file: %s\n", client_files[index]);
          c_get(fd, client_files[index]);
        }
      }
      else
      {
        #ifdef DEBUG_MODE
          printf("DEBUG: Files are the same on client and server\n");
          fflush(stdout);
        #endif
      }
    }
  }

  #ifdef DEBUG_MODE
    printf("DEBUG: Checking against %d client files.\n", client_tracked);
    fflush(stdout);
  #endif

  // Does pretty much the same thing as the previous loop, just the opposite.
  for(a = 0; a < client_tracked; a++)
  {
    int index = getArrayIndex(server_files, client_files[a], server_tracked);
    #ifdef DEBUG_MODE
      printf("DEBUG: file:\t%s index:\t%d\n", client_files[a], index);
      fflush(stdout);
    #endif
    if(index == -1)
    {
      // Upload file to server because it doesn't exist on server.
      printf("[client] Uploading file to server: %s\n", client_files[a]);
      c_put(fd, client_files[a]);
    }
    // else
    // {
    //   // Query and download as appropriate
    //   if(strncmp(server_hashes[index], client_hashes[a], 16) != 0)
    //   {
    //     bool todo = c_query(fd, client_files[a]);
    //     if(todo)
    //     {
    //       c_put(fd, client_files[a]);
    //     }
    //     else
    //     {
    //       c_get(fd, client_files[a], server_hashes[index]);
    //     }
    //   }
    //   else
    //   {
    //     #ifdef DEBUG_MODE
    //       printf("DEBUG: Files are the same on client and server\n");
    //       fflush(stdout);
    //     #endif
    //   }
    // }
  }

  close(fd);
}

// Gets the index of toFind in the array inHere searching a max of max indices.
// Returns -1 if toFind is not found in the array.
int getArrayIndex(char** inHere, char* toFind, int max)
{
  #ifdef DEBUG_MODE
    printf("DEBUG: Finding %s\n", toFind);
    fflush(stdout);
  #endif

  int a = 0;
  for(; a < max; a++)
  {
    if(strcmp(inHere[a], toFind) == 0)
    {
      return a;
    }
  }
  return -1;
}

// Sends the query command to the server given a filename.
// Returns 1 if the file needs to be put, 0 for get.
bool c_query(int fd, char* filename)
{
  #ifdef DEBUG_MODE
    printf("DEBUG: Querying file:\t%s\n", filename);
    fflush(stdout);
  #endif

  struct stat clientFile;
  stat(filename, &clientFile);

  int mtime_client = clientFile.st_mtime;

  char toSend[300];
  sprintf(toSend, "query %s %d", filename, mtime_client);
  send(fd, toSend, strlen(toSend), 0);

  // This is an overkill buffer but w/e.
  char buffer[BUFFER_SIZE];
  int total = 0;
  do
  {
    int recvBytes = recv(fd, buffer+total, sizeof(buffer)-total, 0);
    #ifdef DEBUG_MODE
      printf("DEBUG: Recieved %d bytes for %d total\n", recvBytes, recvBytes + total);
      fflush(stdout);
    #endif
    total += recvBytes;
  }
  while(total < 3);

  #ifdef DEBUG_MODE
    printf("DEBUG: Recieved %3s\n", buffer);
    fflush(stdout);
  #endif

  return strncmp(buffer, "put", 3) == 0;
}

// Does get for filename on the server.
void c_get(int fd, char* filename)
{
  #ifdef DEBUG_MODE
    printf("DEBUG: Doing get %s\n", filename);
    fflush(stdout);
  #endif

  // This doesn't work.
  // printf("[client] Downloading %s: ", filename);
   int a = 0;
  // for(; a < 16; a++)
  // {
  // printf("%02x", (unsigned char) serverHash[a]);
  // }
  // printf("\n");

  char toSend[300];
  sprintf(toSend, "get %s", filename);

  #ifdef DEBUG_MODE
    printf("DEBUG: toSend packet: %s\n", toSend);
    fflush(stdout);
  #endif

  send(fd, toSend, strlen(toSend), 0);

  // Recieve the size of the file to get
  char buffer[BUFFER_SIZE];
  buffer[recv(fd, buffer, sizeof(buffer), 0)] = 0;
  int size = atoi(buffer);

  #ifdef DEBUG_MODE
    printf("DEBUG: Got filesize %d\n", size);
    fflush(stdout);
  #endif

  // Acknowledge we got the filesize, also tells the server to start Sending
  // data.
  send(fd, "ACK", 3, 0);

  // Recvs until the appropriate number of bytes has been recieved.
  char* toWrite = calloc(sizeof(char), size);
  int total = 0;
  do {
    int recvBytes = recv(fd, toWrite+total, size-total, 0);
    #ifdef DEBUG_MODE
      printf("DEBUG: Recieved %d bytes for %d total versus %d\n", recvBytes, recvBytes + total, size);
      fflush(stdout);
    #endif
    total += recvBytes;
  } while(total < size);

  remove(filename);

  #ifdef DEBUG_MODE
    printf("DEBUG: Delete old file\n");
    fflush(stdout);
  #endif

  FILE* writeFile = fopen(filename, "wb");

  #ifdef DEBUG_MODE
    printf("DEBUG: Opened file for writing\n");
    fflush(stdout);
  #endif

  // Writes from the packet to the file.
  for(a = 0; a < size; a++)
  {
    fprintf(writeFile, "%c", toWrite[a]);
  }
  fclose(writeFile);

  #ifdef DEBUG_MODE
    printf("DEBUG: Wrote file\n");
    fflush(stdout);
  #endif
}

// Runs put for filename to upload to the server.
void c_put(int fd, char* filename)
{
  #ifdef DEBUG_MODE
    printf("DEBUG: Uploading %s\n", filename);
    fflush(stdout);
  #endif

  struct stat fileStat;
  stat(filename, &fileStat);

  int size = fileStat.st_size;

  char toSend[300];

  memset(toSend, 0, 300);

  // Sends the filename and size to put.
  sprintf(toSend, "put %s %d", filename, size);

  #ifdef DEBUG_MODE
    printf("DEBUG: Sending %s of size %d\n", filename, size);
    fflush(stdout);
  #endif

  send(fd, toSend, strlen(toSend), 0);

  // Wait for the server to acknowledge that it is ready to recieve size bytes.
  char buffer[16];
  recv(fd, buffer, sizeof(buffer), 0);

  if(strncmp(buffer, "ACK", 3) == 0)
  {
    #ifdef DEBUG_MODE
      printf("DEBUG: Recieved ACK\n");
      fflush(stdout);
    #endif

    char* sendFile = calloc(sizeof(char), size);
    FILE* readFile = fopen(filename, "rb");

    #ifdef DEBUG_MODE
      if(readFile == NULL)
        printf("DEBUG: NULL READ FILE\n");
    #endif

    // Copy the contents of the file into a char* packet.

    int a = 0;
    char temp[2];
    temp[1] = 0;
    for(; a < size; a++)
    {
      fscanf(readFile, "%c", temp);
      memcpy(sendFile+a, temp, 1);
    }
    fclose(readFile);

    #ifdef DEBUG_MODE
      printf("DEBUG: Sending file\n");
      fflush(stdout);
    #endif

    send(fd, sendFile, size, 0);
  }
  else
  {
    #ifdef DEBUG_MODE
      printf("DEBUG: Didn't recieve ACK on put\n");
      printf("DEBUG: Recieved %s\n", buffer);
      fflush(stdout);
    #endif
    return;
  }

  #ifdef DEBUG_MODE
    printf("DEBUG: Finished putting %s\n", filename);
    fflush(stdout);
  #endif
}

// Runs the contents command on the server, parses the data and puts it into
// the given files and hashes arrays.
int c_contents(int fd, char*** files, char*** hashes)
{
  char** filenames = (*files);
  char** filehashes = (*hashes);

  char buffer[BUFFER_SIZE];

  #ifdef DEBUG_MODE
    printf("DEBUG: Sending contents\n");
    fflush(stdout);
  #endif

  send(fd, "contents", 8, 0);

  #ifdef DEBUG_MODE
    printf("DEBUG: Recieving size buffer\n");
    fflush(stdout);
  #endif

  // Find out what the size of the contents data.
  recv(fd, buffer, sizeof(buffer), 0);

  int contentSize;
  sscanf(buffer, "%d", &contentSize);

  #ifdef DEBUG_MODE
    printf("DEBUG: size:\t%d\n", contentSize);
    fflush(stdout);
  #endif

  char* recvBuffer = calloc(sizeof(char), contentSize);
  #ifdef DEBUG_MODE
    printf("DEBUG: Sending ACK\n");
    fflush(stdout);
  #endif

  // Notify the server that its okay to start sending
  send(fd, "ACK", 3, 0);

  #ifdef DEBUG_MODE
    printf("DEBUG: Recieving buffer\n");
    fflush(stdout);
  #endif

  // Recieve bytes until we have all of them.
  int total = 0;
  do {
    int recvBytes = recv(fd, recvBuffer+total, contentSize-total, 0);
    #ifdef DEBUG_MODE
      printf("DEBUG: Recieved %d bytes for %d total\n", recvBytes, recvBytes + total);
      fflush(stdout);
    #endif
    total += recvBytes;
  } while(total < contentSize);

  #ifdef DEBUG_MODE
    printf("DEBUG: Recieved buffer\n");
    printf("DEBUG: %64s\n", recvBuffer);
    fflush(stdout);
  #endif

  char thash[32];
  char tfilename[256];

  memset(thash, 0, 32);
  memset(tfilename, 0, 32);

  int tracked = 0;

  char* substring = strtok(recvBuffer, "\n");

  while(substring != NULL)
  {
    sscanf(substring, "%s    %s", thash, tfilename);
    #ifdef DEBUG_MODE
      printf("DEBUG: contents hash:\t%s file:\t%s\n", thash, tfilename);
      fflush(stdout);
    #endif
    filenames[tracked] = calloc(sizeof(char), strlen(tfilename));
    memcpy(filenames[tracked], tfilename, strlen(tfilename));
    filehashes[tracked] = calloc(sizeof(char), 32);
    #ifdef DEBUG_MODE
      printf("DEBUG: file: %s\thash: %s\n", tfilename, deHex(thash));
      fflush(stdout);
    #endif
    memcpy(filehashes[tracked], deHex(thash), 32);
    tracked += 1;
    substring = strtok(NULL, "\n");
    memset(thash, 0, 32);
    memset(tfilename, 0, 32);
  }

  #ifdef DEBUG_MODE
    printf("DEBUG: Server Files Tracked %d\n", tracked);
    fflush(stdout);
  #endif

  return tracked;
}

// Converts a two-character hex hash back to a 16 byte char hash
// Does this via math.
char* deHex(char* hash)
{
  char* unhexed = calloc(sizeof(char), 16);

  int a = 0;
  for(; a < 16; a++)
  {
    int sixteens;

    if(hash[a] > 47 && hash[a] < 58)
    {
      sixteens = 16 * (hash[a] - 48);
    }
    else
    {
      sixteens = 16 * (hash[a] - 87);
    }

    int ones;

    if(hash[a+1] > 47 && hash[a+1] < 58)
    {
      ones = 1 * (hash[a+1] - 48);
    }
    else
    {
      ones = 1 * (hash[a+1] - 87);
    }
    unhexed[a] = (unsigned char)((sixteens + ones));
  }

  return unhexed;
}

// Finds the contents of the current directory and stores the filenames and hashes
// in the given arrays.
int c_dircontents(char*** files, char*** hashes)
{
  char** fileNames = *files;
  char** fileHashes = *hashes;
  int fileCount = 0;

  DIR *dir;
  struct dirent *ent;
  dir = opendir(".");
  while ((ent = readdir(dir)) != NULL)
  {
    char* programName = self + (strlen(self) - strlen(ent->d_name));
    if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0 || strcmp(ent->d_name, programName) == 0)
    {
      // Do nothing
    }
    else
    {
      if(ent->d_type != DT_DIR)
      {
        #ifdef DEBUG_MODE
          printf("DEBUG: Getting size and hash of %s\n", ent->d_name);
          fflush(stdout);
        #endif
        fileNames[fileCount] = calloc(sizeof(char), strlen(ent->d_name));
        memcpy(fileNames[fileCount], ent->d_name, strlen(ent->d_name));
        fileHashes[fileCount] = hashFile(ent->d_name);
        #ifdef DEBUG_MODE
          printf("DEBUG: %s : %s\n", fileNames[fileCount], fileHashes[fileCount]);
          fflush(stdout);
        #endif
        fileCount+=1;
      }
    }
  }
  closedir(dir);

  #ifdef DEBUG_MODE
    printf("DEBUG: Client Files Tracked %d\n", fileCount);
    fflush(stdout);
  #endif

  return fileCount;
}

// Binds server and passes all incoming connection fds to server_thread
int server(int port)
{
  #ifdef DEBUG_MODE
    printf("DEBUG: Server started with port %d\n", port);
    fflush(stdout);
  #endif

  int tfd_server = socket(AF_INET, SOCK_STREAM, 0);
  if(tfd_server == -1)
  {
    fprintf(stderr, "ERROR: Error on TCP socket() call\n");
    return EXIT_FAILURE;
  }

  struct sockaddr_in tsock_server;
  socklen_t tsocklen_server = sizeof(tsock_server);
  tsock_server.sin_family = AF_INET;
  tsock_server.sin_addr.s_addr = INADDR_ANY;
  tsock_server.sin_port = htons(port);

  if(bind(tfd_server, (struct sockaddr*)&tsock_server, tsocklen_server) < 0)
  {
    fprintf(stderr, "ERROR: Error on TCP bind() call\n");
    return EXIT_FAILURE;
  }

  if(getsockname(tfd_server, (struct sockaddr*)&tsock_server, &tsocklen_server) < 0)
  {
    fprintf(stderr, "ERROR: Error on TCP getsockname() call\n");
    return EXIT_FAILURE;
  }

  listen(tfd_server, 5);

  #ifdef DEBUG_MODE
    printf("DEBUG: Server binded port %d successfully\n", port);
    fflush(stdout);
  #endif

  char template[] = "tempXXXXXX";
  char* dir = mkdtemp(template);
  if(dir == NULL)
  {
    fprintf(stderr, "ERROR: Error on temporary directory creation.\n");
    return EXIT_FAILURE;
  }

  #ifdef DEBUG_MODE
    printf("DEBUG: temporary directory name:\t%s\n", dir);
    fflush(stdout);
  #endif

  if(chdir(dir) == -1)
  {
     fprintf(stderr, "ERROR: Could not change directory to temporary directory.\n");
     return EXIT_FAILURE;
  }

  FILE* writeFile = fopen(".4220_file_list.txt", "wb");

  if(writeFile == NULL)
  {
    fprintf(stderr, "ERROR: Could not write .4220_file_list.txt\n");
    return EXIT_FAILURE;
  }

  fclose(writeFile);

  while(1)
  {
    struct sockaddr_in tsock_client;
    socklen_t tsocklen_client = sizeof(tsock_client);

    server_thread(accept(tfd_server, (struct sockaddr*)&tsock_client, &tsocklen_client));
  }
}

int server_thread(int fd)
{
  int a = 0;

  #ifdef DEBUG_MODE
    printf("DEBUG: New TCP connection on %d\n", fd);
    fflush(stdout);
  #endif

  while(1)
  {
    // Recieve commands and split as appropriate.
    char buffer[BUFFER_SIZE];
    int readBytes = recv(fd, buffer, sizeof(buffer), 0);

    #ifdef DEBUG_MODE
      printf("DEBUG: Read on %d\n", fd);
      fflush(stdout);
    #endif

    if(readBytes < 1)
    {
      #ifdef DEBUG_MODE
        printf("DEBUG: Closing %d\n", fd);
        fflush(stdout);
      #endif
      close(fd);
      return EXIT_FAILURE;
    }

    if(strncmp(buffer, "contents", 8) == 0)
    {

      #ifdef DEBUG_MODE
        printf("DEBUG: Recieved contents\n");
        fflush(stdout);
      #endif

      struct stat fileStat;
      stat(".4220_file_list.txt", &fileStat);

      int size = fileStat.st_size;

      FILE* toRead = fopen(".4220_file_list.txt", "rb");
      int totalBytes = 0;
      char* packet = calloc(sizeof(char), size);
      char hash[32];
      char filename[256];
      while(fscanf(toRead, "%s    %s\n", hash, filename) != EOF)
      {
        memcpy(packet+totalBytes, hash, 32);
        totalBytes += 32;
        memcpy(packet+totalBytes, "    ", 4);
        totalBytes += 4;
        memcpy(packet+totalBytes, filename, strlen(filename));
        totalBytes += strlen(filename);
        memcpy(packet+totalBytes, "\n", 1);
        totalBytes += 1;
      }
      totalBytes -= 1;
      fclose(toRead);

      char count[15];

      sprintf(count, "%d", totalBytes);

      send(fd, count, strlen(count), 0);

      memset(buffer, 0, BUFFER_SIZE);

      recv(fd, buffer, sizeof(buffer), 0);

      if(strncmp(buffer, "ACK", 3) == 0)
      {
        // Give them the business
        send(fd, packet, totalBytes, 0);

        #ifdef DEBUG_MODE
          printf("DEBUG: Sending: %s", packet);
          fflush(stdout);
        #endif
      }
      else
      {
        // Give them the wrong business
        #ifdef DEBUG_MODE
          fprintf(stderr, "ERROR: Expected ACK packet on contents.\n");
        #endif
        send(fd, "ERROR: Expected ACK packet", 26, 0);
      }
      // free(packet);
    }
    else if(strncmp(buffer, "query", 5) == 0)
    {
      char filename[256];
      int mtime_client;
      sscanf(buffer, "%*s %s %d", filename, &mtime_client);
      struct stat serverFile;
      stat(filename, &serverFile);

      int mtime_server = serverFile.st_mtime;

      if(mtime_client > mtime_server)
      {
        #ifdef DEBUG_MODE
          printf("DEBUG: Client needs to put\n");
          fflush(stdout);
        #endif

        // Client needs to put.
        send(fd, "put", 3, 0);
      }
      else
      {
        #ifdef DEBUG_MODE
          printf("DEBUG: Client needs to get\n");
          fflush(stdout);
        #endif

        // Client needs to get.
        send(fd, "get", 3, 0);
      }
    }
    else if(strncmp(buffer, "get", 3) == 0)
    {
      char filename[256];
      sscanf(buffer, "%*s %s", filename);

      #ifdef DEBUG_MODE
        printf("DEBUG: getting file:\t%s\n", filename);
        fflush(stdout);
      #endif

      struct stat serverFile;
      stat(filename, &serverFile);
      int size = serverFile.st_size;

      #ifdef DEBUG_MODE
        printf("DEBUG: filesize:\t%d\n", size);
        fflush(stdout);
      #endif

      char count[15];
      sprintf(count, "%d", size);

      send(fd, count, strlen(count), 0);

      recv(fd, buffer, sizeof(buffer), 0);

      if(strncmp(buffer, "ACK", 3) == 0)
      {

        #ifdef DEBUG_MODE
          printf("DEBUG: got ACK\n");
          fflush(stdout);
        #endif

        // Give them the business.
        FILE* toRead = fopen(filename, "rb");
        char* packet = calloc(sizeof(char), size);
        char temp[2];
        temp[1] = 0;
        int a = 0;
        while(fscanf(toRead, "%c", temp) != EOF)
        {
          memcpy(packet+a, temp, 1);
          a+=1;
        }
        fclose(toRead);
        send(fd, packet, size, 0);

        #ifdef DEBUG_MODE
          printf("DEBUG: sent file contents\n");
          fflush(stdout);
        #endif
      }
      else
      {
        // Don't give them the business.
        #ifdef DEBUG_MODE
          fprintf(stderr, "ERROR: Expected ACK packet on get.\n");
        #endif
        send(fd, "ERROR: Expected ACK packet", 26, 0);
      }

      reHash();
    }
    else if(strncmp(buffer, "put", 3) == 0)
    {

      #ifdef DEBUG_MODE
        printf("DEBUG: Recieved put\n");
        fflush(stdout);
      #endif

      char filename[256];
      int size;
      sscanf(buffer, "%*s %s %d", filename, &size);

      printf("[server] Detected different & newer file: %s\n", filename);

      #ifdef DEBUG_MODE
        printf("DEBUG: file:\t%s size:\t%d\n", filename, size);
        fflush(stdout);
      #endif

      remove(filename);

      send(fd, "ACK", 3, 0);

      #ifdef DEBUG_MODE
        printf("DEBUG: Sent ACK\n");
        fflush(stdout);
      #endif

      char* packet = calloc(sizeof(char), size);
      int total = 0;
      do {
        int recvBytes = recv(fd, packet+total, size-total, 0);
        #ifdef DEBUG_MODE
          printf("DEBUG: Recieved %d bytes for %d total versus %d\n", recvBytes, recvBytes + total, size);
          fflush(stdout);
        #endif
        total += recvBytes;
      } while(total < size);

      #ifdef DEBUG_MODE
        printf("DEBUG: Recieved packet\n");
        fflush(stdout);
      #endif

      FILE* toWrite = fopen(filename, "wb");
      for(a = 0; a < size; a++)
      {
        fprintf(toWrite, "%c", packet[a]);
      }
      fclose(toWrite);

      #ifdef DEBUG_MODE
        printf("DEBUG: Wrote file\n");
        fflush(stdout);
      #endif

      char* hash = hashFile(filename);

      printf("[server] Downloading %s: ", filename);
      for(a = 0; a < 16; a++)
      {
        printf("%02x", (unsigned char) hash[a]);
      }
      printf("\n");

      reHash();
    }
    else
    {
      #ifdef DEBUG_MODE
        printf("DEBUG: Unexpected command\n");
        printf("DEBUG: Unexpected command [%s]\n", buffer);
        fflush(stdout);
      #endif
    }
    memset(buffer, 0, BUFFER_SIZE);
  }
}

// Generates the contents of .4220_file_list.txt again
void reHash()
{
  #ifdef DEBUG_MODE
    printf("DEBUG: Started rehash\n");
    fflush(stdout);
  #endif

  remove(".4220_file_list.txt");
  FILE* toWrite = fopen(".4220_file_list.txt", "wb");
  DIR *dir;
  struct dirent *ent;
  dir = opendir(".");
  while ((ent = readdir(dir)) != NULL)
  {
    char* programName = self + (strlen(self) - strlen(ent->d_name));
    if(strcmp(ent->d_name, ".4220_file_list.txt") != 0 && strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0 && strcmp(ent->d_name, programName) != 0)
    {
      #ifdef DEBUG_MODE
        printf("DEBUG: rehash processing %s\n", ent->d_name);
        fflush(stdout);
      #endif
      char* fileHash = hashFile(ent->d_name);

      #ifdef DEBUG_MODE
        printf("DEBUG: file: %s\thash: %s\n", ent->d_name, fileHash);
        fflush(stdout);
      #endif

      int a = 0;
      for(; a < 16; a++)
      {
        fprintf(toWrite, "%02x", (unsigned char) fileHash[a]);
      }
      fprintf(toWrite, "    %s\n", ent->d_name);
      free(fileHash);
    }
  }
  closedir(dir);
  fclose(toWrite);

  #ifdef DEBUG_MODE
    printf("DEBUG: Ended rehash\n");
    fflush(stdout);
  #endif
}

// Returns the unsigned character hash of the given file.
char* hashFile(char* filename)
{
  FILE* toRead = fopen(filename, "rb");

  struct stat fileStat;
  stat(filename, &fileStat);

  int size = fileStat.st_size;

  char* readIn = calloc(sizeof(char), size);
  int a = 0;
  char temp[2];
  temp[1] = 0;
  for(; a < size; a++)
  {
    if(fscanf(toRead, "%c", temp) != EOF)
    {
      memcpy(readIn+a, temp, 1);
    }
    else
    {
      if(a == 0)
      {
        #ifdef DEBUG_MODE
          printf("DEBUG: file %s empty\n", filename);
          fflush(stdout);
        #endif
        memcpy(readIn, "EMPTY", 5);
      }
      break;
    }
  }
  fclose(toRead);

  char* hash = calloc(sizeof(char), 16);
  MD5((const unsigned char *)readIn, a, (unsigned char *) hash);
  return hash;
}
