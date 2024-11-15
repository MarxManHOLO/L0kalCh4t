
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <gmp.h>

#include <settings.h>
#include <rsa.h>
#include <comm.h>
#include <server.h>
#include <client_manager.h>


static unsigned num_clients = 0;

static client_entry_t* client_list = NULL;


/* this procedure is called only
   once at the start of the program */
void init_client_manager(const int server_socket)
{
  // create client_entry_t to hold server socket at very beginning of list
  client_list = calloc(1, sizeof(client_entry_t));
  char ip[20] = "0.0.0.0";
  char uname[UNAMELEN] = "SERVER";

  // fill in full server entry except rsa key, no need for it to be here
  client_list->socket = server_socket;
  memcpy(client_list->ip, ip, 20);
  memcpy(client_list->uname, uname, UNAMELEN);

  // set heartbeat handler
  if (signal(SIGALRM, heartbeat) == SIG_ERR)
  {
    perror("server(): signal()");
    exit(EXIT_FAILURE);
  }

  // set timer
  alarm(30);
}


/* retrieves a client with matching socket number */
static client_entry_t* get_client_with_socket(int socket)
{
  client_entry_t* iterator = client_list;

  while (iterator)
  {
    if (iterator->socket == socket)
      return iterator;
    iterator = iterator->next_entry;
  }

  fprintf(stderr, "get_client_with_socket(): no client with socket number %d\n", socket);
  return NULL;
}


/* called from handle_client_message() to send an incoming message from
   one client to all other connected clients */
static int broadcast(const char* uname, const char* msg)
{
  // generate timestamp
  time_t curr_time;
  struct tm* time_info;
  char time_str[9];

  time(&curr_time);
  time_info = localtime(&curr_time);
  strftime(time_str, sizeof(time_str), "%H:%M:%S", time_info);

  // generate broadcast message
  char outgoing[BROADCASTLEN];
  sprintf(outgoing, "(%s) %s: %s", time_str, uname, msg);

  int result = 0;

  // broadcast (skip server entry)
  client_entry_t* iterator = client_list->next_entry;
  while (iterator)
  {
    // keep trying all of them, even if one fails
    if (send_encrypted_message(iterator->socket, outgoing, strlen(outgoing), iterator->key) == -1)
    {
      fprintf(stderr, "broadcast(): call to send_encrypted_message() failed\n");
      result = -1;

      /* this should never fail */
      if (remove_client(iterator->next_entry->socket) == -1)
      {
        fprintf(stderr, "heartbeat(): call to remove_client() failed\n");
        exit(-1);
      }
    }

    iterator = iterator->next_entry;
  }

  return result;
}


/* called from server.c to handle incoming 
   messages from existing clients */
int handle_client_message(const int socket, const rsa_key_t privkey)
{
  // retrieve client with this socket
  client_entry_t* client = get_client_with_socket(socket);
  if (client == NULL)
  {
    fprintf(stderr, "handle_client_message(): call to get_client_with_socket() failed\n");
    return -1;
  }

  // read in message from client
  char msg[MAXMSGLEN + 1];
  memset(msg, 0, MAXMSGLEN);
  int len = receive_encrypted_message(socket, msg, MAXMSGLEN, privkey);

  // check for error
  if (len == -1)
  {
    fprintf(stderr, "handle_client_message(): call to receive_encrypted_message() failed\n");
    return -1;
  }

  // check if client has disconnected
  if (strcmp(msg, "DISCONNECT\n") == 0 || len == 0)
  {
    fprintf(stdout, "%s disconnected\n", client->uname);
    if (remove_client(socket) == -1)
    {
      fprintf(stderr, "handle_client_message(): call to remove_client() failed\n");
      return -1;
    }
    return 0;
  }

  // check if this is heartbeat message
  if (strcmp(msg, "HEARTBEAT\n") == 0)
  {
    client->missed_beats = 0;
    return 0;
  }

  // otherwise, broadcast the message everywhere
  if (broadcast(client->uname, msg) == -1)
  {
    fprintf(stderr, "handle_client_message(): call to broadcast() failed\n");
    return -1;
  }

  return 0;
}


/* called by new_connection() to check if user name
   already exists */
static int validate_uname(const char* uname)
{
  client_entry_t* iterator = client_list;

  while (iterator)
  {
    if (strcmp(uname, iterator->uname) == 0)
      return -1;

    iterator = iterator->next_entry;
  }

  return 0;
}


int new_connection(const int socket, const char* ip, rsa_key_t pubkey)
{
  // read message from new connection
  char msg[RECVBUFFLEN + 1];
  memset(msg, 0, RECVBUFFLEN + 1);
  char *field_ptr;

  // return if something went wrong while reading from socket
  if (receive_message(socket, msg, RECVBUFFLEN) == -1)
  {
    fprintf(stderr, "new_connection(): call to receive_message() failed\n");
    return -1;
  }

  rsa_key_t client_key;  

  // check if BASE field is present
  if ((field_ptr = strstr(msg, "BASE: ")) == NULL)
  {
    fprintf(stderr, "new_connection(): request missing BASE field\n");

    char* err_resp = "Missing BASE field\n";
    if (send_message(socket, err_resp, strlen(err_resp)) == -1)
      fprintf(stderr, "new_connection(): failed to send error response\n");
    return -1;
  }

  // scan in BASE
  char base_str[BASE_FLD_SZ];
  sscanf(field_ptr, BASE_FMT, base_str);
  int base = strtol(base_str, NULL, 10);

  // validate base
  if (base < 2 || base > 62)
  {
    fprintf(stderr, "new_connection(): request contains invalid BASE value\n");

    char* err_resp = "Invalid BASE value\n";
    if (send_message(socket, err_resp, strlen(err_resp)) == -1)
      fprintf(stderr, "new_connection(): failed to send error response\n");
    return -1;
  }

  client_key->b = base;

  // check if EXP field is present
  if ((field_ptr = strstr(msg, "EXP: ")) == NULL)
  {
    fprintf(stderr, "new_connection(): request missing EXP field\n");

    char* err_resp = "Missing EXP field\n";
    if (send_message(socket, err_resp, strlen(err_resp)) == -1)
      fprintf(stderr, "new_connection(): failed to send error response\n");
     return -1;
  }

  // scan in EXP value
  char exponent[EXP_FLD_SZ];
  sscanf(field_ptr, EXP_FMT, exponent);

  // validate exponent
  mpz_t exp;
  mpz_init(exp);
  mpz_set_str(exp, exponent, base);
  if (mpz_cmp_ui(exp, 0) == 0)
  {
    fprintf(stderr, "new_connection(): request contains invalid EXP value\n");

    mpz_clear(exp);
    char* err_resp = "Invalid EXP value\n";
    if (send_message(socket, err_resp, strlen(err_resp)) == -1)
      fprintf(stderr, "new_connection(): failed to send error response\n");
    return -1;
  }
  mpz_clear(exp);

  client_key->e = exponent;

  // check if DIV field is present
  if ((field_ptr = strstr(msg, "DIV: ")) == NULL)
  {
    fprintf(stderr, "new_connection(): request missing DIV field\n");

    char* err_resp = "Missing DIV field\n";
    if (send_message(socket, err_resp, strlen(err_resp)) == -1)
      fprintf(stderr, "new_connection(): failed to send error response\n");
    return -1;
  }

  // scan in DIV value
  char divisor[DIV_FLD_SZ];
  sscanf(field_ptr, DIV_FMT, divisor);

  // validate divisor
  mpz_t div;
  mpz_init(div);
  mpz_set_str(div, divisor,  base);
  if (mpz_cmp_ui(div, 0) == 0)
  {
    fprintf(stderr, "new_connection(): request contains invalid DIV value\n");

    mpz_clear(div);
    char* err_resp = "Invalid DIV value\n";
    if (send_message(socket, err_resp, strlen(err_resp)) == -1)
      fprintf(stderr, "new_connection(): failed to send error response\n");
    return -1;
  }
  mpz_clear(div);

  client_key->d = divisor;

  // check if we are at max capacity
  if (num_clients == MAXCLIENTS)
  {
    fprintf(stderr, "new_connection(): server at max client capacity\n");

    char* err_resp = "Server at max capacity\n";
    send_encrypted_message(socket, err_resp, strlen(err_resp), client_key);
    return -1;
  }

  // check if UNAME field is present
  if ((field_ptr = strstr(msg, "UNAME: ")) == NULL)
  {
    fprintf(stderr, "new_connection(): request missing UNAME field\n");

    char* err_resp = "Missing UNAME field\n";
    if (send_encrypted_message(socket, err_resp, strlen(err_resp), client_key) == -1)
      fprintf(stderr, "new_connection(): failed to send error response\n");
    return -1;    
  }

  // scan in UNAME
  char uname[UNAME_FLD_SZ + 1];
  sscanf(field_ptr, UNAME_FMT, uname);

  // validate UNAME
  if (validate_uname(uname) == -1)
  {
    fprintf(stderr, "new_connection(): username already exists\n");

    char* err_resp = "Username already exists\n";
    if (send_encrypted_message(socket, err_resp, strlen(err_resp), client_key) == -1)
      fprintf(stderr, "new_connection(): failed to send error response\n");
    return -1;
  }

  // create new client and fill out entry
  client_entry_t* new_entry = malloc(sizeof(client_entry_t));

  new_entry->next_entry = NULL;
  new_entry->socket = socket;
  new_entry->key->b = base;

  memcpy(new_entry->ip, ip, strlen(ip) + 1);
  memcpy(new_entry->uname, uname, strlen(uname) + 1);

  new_entry->key->e = calloc(1, strlen(exponent) + 1);
  new_entry->key->d = calloc(1, strlen(divisor) + 1);

  memcpy(new_entry->key->e, exponent, strlen(exponent));
  memcpy(new_entry->key->d, divisor, strlen(divisor));

  // looks good. prepare server info
  char response[RECVBUFFLEN];
  char* template = ACCEPT_RESP_TMPLT;
  int len = sprintf(response,
          template,
          pubkey->b,
          pubkey->e,
          pubkey->d);

  // encrypt using client key, then send
  if (send_encrypted_message(socket, response, len, new_entry->key) == -1)
  {
    fprintf(stderr, "new_connection(): call to send_encrypted_message() failed\n");

    rsa_clear_key(new_entry->key);
    free(new_entry);
    return -1;
  }

  // find end of client list
  client_entry_t* last = client_list;
  while (last->next_entry)
    last = last->next_entry;

  // append client entry
  last->next_entry = new_entry;
  num_clients++;

  // notify chat
  template = "%s joined the chat";
  char joinmsg[strlen(template) + strlen(uname)];
  sprintf(joinmsg, template, uname);

  if (broadcast("SERVER", joinmsg) == -1)
  {
    fprintf(stderr, "new_connection(): call to broadcast() failed\n");
    return -1;
  }
  printf("%s\n", joinmsg);

  return 0;
}


static int remove_client(const int socket)
{
  client_entry_t* iterator = client_list;

  // check if first entry is the one to be removed
  if (client_list->socket == socket)
  {
    // skip over first entry
    client_list = client_list->next_entry;

    // then free it up
    rsa_clear_key(iterator->key);
    free(iterator);
  }

  // look for entry with matching socket number
  while (iterator->next_entry != NULL && 
         iterator->next_entry->socket != socket)
    iterator = iterator->next_entry;

  // check if we have reached the end
  if (iterator->next_entry == NULL)
  {
    fprintf(stderr, "remove_client(): socket %d does not exist in client list\n", socket);
    return -1;
  }

  // save next entry, set pointer to next entry to next next entry
  client_entry_t* deletethis = iterator->next_entry;
  iterator->next_entry = deletethis->next_entry;

  // notify chat
  char *template = "%s left the chat";
  char leavemsg[strlen(template) + strlen(deletethis->uname)];
  sprintf(leavemsg, template, deletethis->uname);
  broadcast("SERVER", leavemsg);

  // free up old entry
  rsa_clear_key(deletethis->key);
  free(deletethis);

  // close socket
  close(socket);

  // decrement client counter
  num_clients--;

  return 0;
}


int initialize_fdset(fd_set* fds)
{
  int max = 0;

  // zero out socket set
  FD_ZERO(fds);

  /* iterate over each client entry, adding each socket to socket set,
     and also keeping track of highest socket value */
  client_entry_t* iterator = client_list;
  while (iterator)
  {
    // get socket from client entry
    int socket = iterator->socket;

    // add socket to set
    FD_SET(socket, fds);

    // check if current socket is the highest we've seen
    if (socket > max) max = socket;

    // get next client entry
    iterator = iterator->next_entry;
  }

  return max; 
}


int get_active_fd(fd_set* fds)
{
  // iterate over client entry list and find active socket
  client_entry_t* iterator = client_list;
  while (iterator)
  {
    int socket = iterator->socket;
    if (FD_ISSET(socket, fds))
      return socket;
    iterator = iterator->next_entry;
  }

  fprintf(stderr, "get_active_fd(): could not find active socket\n");
  return -1;
}


/* iterate over every client, send heartbeat, increment missed beats */
void heartbeat(int sig)
{
  client_entry_t* iterator = client_list;

  /* loop with previous entry in case curr gets removed */
  while (iterator->next_entry)
  {
    if (iterator->next_entry->missed_beats == 3)
    {
      fprintf(stderr, "heartbeat(): %s (%s) timed out\n", iterator->uname, iterator->ip);

      /* this should never fail */
      if (remove_client(iterator->next_entry->socket) == -1)
      {
        fprintf(stderr, "heartbeat(): call to remove_client() failed\n");
        exit(-1);
      }

      continue;
    }

    char *msg = "HEARTBEAT\n";
    if (send_encrypted_message(iterator->next_entry->socket,
                               msg,
                               strlen(msg),
                               iterator->next_entry->key) == -1)
    {
      fprintf(stderr, "heartbeat(): call to send_encrypted_message() failed\n");

      /* this should never fail */
      if (remove_client(iterator->next_entry->socket) == -1)
      {
        fprintf(stderr, "heartbeat(): call to remove_client() failed\n");
        exit(-1);
      }
    }

    /* increment missed beats */
    iterator->next_entry->missed_beats++;
    iterator = iterator->next_entry;
  }

  // reset timer
  alarm(30);
}

//By Seyran Almammadli
