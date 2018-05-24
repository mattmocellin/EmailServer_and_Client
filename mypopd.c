#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024

static void handle_client(int fd);
void printNotSupp(int fd);
void printInvalid(int fd);
void printQuit(int fd); 
void notValid(int fd, int type);
char* trim_white_space(char *str);
char* check_args(char* str);
int numbers_only(const char *s);
char* clean_buf(char*);


int runServer;

// Various server states
enum states { AUTH0 = 0, AUTH1, TRANS, UPDATE};
enum types { USER = 0, PASS};
enum states serverState;

char user[MAX_LINE_LENGTH];
mail_list_t user_mail;
user_list_t ul; 

int main(int argc, char *argv[]) {
  
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments. Expected: %s <port>\n ", argv[0]);
    return 1;
  }
  
  run_server(argv[1], handle_client);
  
  return 0;
}


void handle_client(int fd) {

  send_string(fd, "+OK Server ready\r\n");
  serverState = AUTH0;
  runServer = 1;

  size_t  maxSize = MAX_LINE_LENGTH;
  net_buffer_t nb = nb_create(fd, MAX_LINE_LENGTH);
  char buf[maxSize];


  while(runServer) {

    // char* cleanBuf;


    int sig_int = nb_read_line(nb, buf);

    // strcpy(cleanBuf, buf);

    // printf("%s\n", "here");

    // clean_buf(cleanBuf);

    //     printf("%s\n", "here2");


    if (sig_int == 0) {
      exit(0);
    } else if (sig_int == 1) {
      exit(1);
    }

    printf(buf);

    switch (serverState) {

      // Auth - USER -----------------------------------------------------------------------------------------------------------------

      case 0:
      if (strncasecmp(buf, "USER ", 5) == 0) {
        char* tempUser = check_args(buf);

        if (tempUser) {
          trim_white_space(tempUser);
          strcpy(user, tempUser);

          if (is_valid_user(user, NULL) != 0) {
            send_string(fd,"+OK User valid\r\n");
            serverState = AUTH1;
          } else notValid(fd, USER);
        } else notValid(fd, USER);

      } else if (strncasecmp(buf, "APOP", 4) == 0) {
          printNotSupp(fd);

      } else if (strncasecmp(buf, "QUIT", 4) == 0) {
          printQuit(fd);
          serverState = AUTH0;
          exit(0);

      } else {
          printInvalid(fd);
      }

      break;

      // Auth - PASS ---------------------------------------------------------------------------------------------------
      case 1:
      if (strncasecmp(buf, "PASS ", 5) == 0) {
        char* pass = check_args(buf);

        if (pass) {

          pass = trim_white_space(pass);

          if (is_valid_user(user, pass) != 0) {
            send_string(fd,"+OK Password valid\r\n");

            add_user_to_list(&ul, user);
            user_mail = load_user_mail(user);
            serverState = TRANS;
          } else notValid(fd, PASS);
        } else notValid(fd, PASS);

      } else if (strncasecmp(buf, "QUIT", 4) == 0) {
          printQuit(fd);
          serverState = AUTH0;
          exit(0);

      } else {
          printInvalid(fd);
          serverState = AUTH0;
      }
      break;

      // Transaction
      case 2:

      // STAT ------------------------------------------------------------------------

      if (strncasecmp(buf, "STAT", 4) == 0) {
        int mailCount = get_mail_count(user_mail);
        int mailListSize = get_mail_list_size(user_mail);

        send_string(fd, "+OK %d %d\r\n", mailCount, mailListSize);        

      // LIST ------------------------------------------------------------------------

      } else if (strncasecmp(buf, "LIST ", 5) == 0) {
        char* arg = check_args(buf);

        int argVal = 0;
        int numflag = 0;

        if (arg) {
        numflag = numbers_only(arg);
        }

        if (!numflag) {
          arg = NULL;
        }

        if (arg) {
          argVal = atoi(arg);
        }

        int mailCount = get_mail_count(user_mail);

        if (argVal) {
          mail_item_t mail = get_mail_item(user_mail, argVal-1);

          if (!mail) {
            send_string(fd, "-ERR No such message, %d messages in mailbox\r\n", mailCount);        

          } else {
            mail_item_t mail = get_mail_item(user_mail, argVal-1);
            size_t mailSize = get_mail_item_size(mail);

            send_string(fd, "+OK %d %zu\r\n", argVal, mailSize);        

          }
        }
           
        } else if (strncasecmp(buf, "LIST", 4) == 0) {

          int mailCount = get_mail_count(user_mail);
          int mailListSize = get_mail_list_size(user_mail);

          if (mailCount != 0) {
            send_string(fd, "+OK %d messages (%d octets)\r\n", mailCount, mailListSize);        

            int i;
            for (i = 0; i < mailCount; i = i+1) {
              mail_item_t mail = get_mail_item(user_mail, i);
              size_t mail_size = get_mail_item_size(mail);

              send_string(fd, "%x %zu\r\n", i+1, mail_size);
            }

            send_string(fd, ".\r\n");

            } else {
              send_string(fd, "+OK\r\n");
              send_string(fd, ".\r\n");

        }

        // RETRIEVE ------------------------------------------------------------------------

        } else if (strncasecmp(buf, "RETR ", 5) == 0) {
          char* arg = check_args(buf);
          int argVal;

        if (arg) {

          argVal = atoi(arg);

        }

        if (arg) {
        mail_item_t mail_item = get_mail_item(user_mail, argVal-1);

        if (mail_item) {

          FILE* file_ptr = NULL;
          const char* filename = get_mail_item_filename(mail_item);    
          file_ptr = fopen(filename, "r");
          int c;
          while(1) {
            c = fgetc(file_ptr);
            if (feof(file_ptr)) break;
            send_string (fd, "%c", c);
          }
          send_string(fd, ".\r\n");

          fclose(file_ptr);
        } else {
          send_string(fd, "-ERR No such message\r\n");
        }

        } else {
          send_string(fd, "-ERR Invalid command\r\n");
        }


        // DELETE ------------------------------------------------------------------------

      } else if (strncasecmp(buf, "DELE ", 4) == 0) {
         char* arg = check_args(buf);
         int argVal;

        if (arg) {
          argVal = atoi(arg);

        mail_item_t mail_item = get_mail_item(user_mail, argVal-1);

        if (mail_item) {
          mark_mail_item_deleted(mail_item);
          send_string(fd, "+OK Message deleted\r\n");
        } else {
          send_string(fd, "-ERR Message already deleted\r\n");
        }
      } else {
        send_string(fd, "-ERR Invalid command");
      }

        // RESET ------------------------------------------------------------------------

      } else if (strncasecmp(buf, "RSET", 4) == 0) {

          reset_mail_list_deleted_flag(user_mail);
          send_string(fd, "+OK\r\n");
        
        // NOOP ------------------------------------------------------------------------


      } else if (strncasecmp(buf, "NOOP", 4) == 0) {

          send_string(fd, "+OK\r\n");

        // QUIT ------------------------------------------------------------------------

      } else if (strncasecmp(buf, "QUIT", 4) == 0) {

          send_string(fd, "+OK POP3 server signing off\r\n");
          serverState = UPDATE;
          exit(0);


      } else if (strncasecmp(buf, "TOP", 3) == 0 || strncasecmp(buf, "UIDL", 4) == 0 ) {
        printNotSupp(fd);

      } else {

        printInvalid(fd);
      }

      break;

      // UPDATE ------------------------------------------------------------------------
      
      case 3:

      destroy_mail_list(user_mail);
      serverState = 0;
      break;


    }
  }

// Various print statements 

}

void printNotSupp(int fd) {
  send_string(fd,"-ERR Command not supported\r\n");
}

void printInvalid(int fd) {
  send_string(fd,"-ERR Command not valid\r\n");
}

void printQuit(int fd) {
  send_string(fd,"+OK POP3 server signing off\r\n");
}

void notValid(int fd, int type) {
  if (type == 0) { 
    send_string(fd,"-ERR User not valid\r\n");
  } else {
    send_string(fd,"-ERR Password not valid\r\n");
    serverState = AUTH0;
  }
}



char* trim_white_space(char* str) {
  char *end;

  // Trim leading space
  while(isspace((unsigned char)*str)) str++;

  if(*str == 0)  
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end)) end--;

  // Write new null terminator
  *(end+1) = 0;

  return str;
}


// grabs first arg given a buffer, if more than one of if null return null

char* check_args(char* buf) {
  buf[strlen(buf)-2] = '\0';

  if ((strncasecmp(buf, " ", 1) == 0)) {
    return NULL;
  }


  char* tokenized_str = strtok(buf, " ");
  char* args;
  int arg_count = 0;

  if (tokenized_str == NULL) {
    args = NULL;
  }

  while(tokenized_str != NULL) {

    if (arg_count == 1) {
      args = tokenized_str;
    } else if ( arg_count > 1) {
      args = NULL;
    }

    arg_count++;
    tokenized_str = strtok(NULL, " ");
  }

  int len = strlen(args);

  if (len == 0) {
    args = NULL;
  }

  return args;
}


int numbers_only(const char *s)
{

    while (*s) {
        if (isdigit(*s++) == 0) return 0;
    }

    return 1;
}


// char* clean_buf(char* buf) {

//     int buflen;

//     while(isspace(buf[buflen-1])) {
//       buflen = strlen(buf);
//       buf[buflen-1] = '\0';
//     }


// }
