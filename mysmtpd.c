#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>
#include <strings.h>

#define MAX_LINE_LENGTH 1024

void printbadSyn(int fd);
void printbadSeq(int fd);
static void handle_client(int fd);

int isRunning; // A variable that determines if a client is currently connected with the server
int nbVal;     // A variable that take indicates the ouput from nb_read_line()

/*
SMTP is composed of 4 different states:
 1. Client intiation: HELO
 2. Mail transactions:
  2.1. MAIL
  2.2. RCPT
  2.3. DATA
*/
enum mailStates {WaitingHelo = 0, WaitingMail = 1, WaitingRCPT = 2, WaitingData = 3};

user_list_t ul; 


int main(int argc, char *argv[]) {
  
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
    return 1;
  }
  
  run_server(argv[1], handle_client);
  
  return 0;
}

void handle_client(int fd) {
  isRunning = 1;
  nbVal = 1;


  int serverState = WaitingHelo;

  struct utsname unameData;
  uname(&unameData);

  size_t  maxSize = MAX_LINE_LENGTH;
  net_buffer_t nb = nb_create(fd,MAX_LINE_LENGTH);
  char foo[maxSize];

  // A client opens a connection to a server, send server greeting/ initiate a SMTP session
  send_string(fd,"220 %s Simple Mail Transfer Service Ready \r\n", unameData.nodename);
	
  // The server waits for client's inputs
  while(isRunning == 1) {


   nbVal = nb_read_line(nb,foo);

   // If the connection between the server and client has been inappropriately disrupted,
   // e.g. "ctrl-c", clean any data from the previous session and declare the connection  
   // with the client has been terminated incorrectly
   if(nbVal == 0) {
    printf("Abrupt termination\r\n");
    nb_destroy(nb);
    if(ul != NULL) {
      destroy_user_list(ul);
    }
    break;
   }

    printf("Client input: %s", foo);

    // If client sends "NOOP", then send a "250 OK" to signify active connection with server
    if(strncasecmp(foo,"NOOP",4) == 0) {
      send_string(fd,"250 OK\r\n");
    }

    // If client sends "QUIT", then send a "221 Okay", clear any stored memory and terminate
    // connection
    if(strncasecmp(foo,"QUIT",4) == 0) {
      send_string(fd,"221 Okay\r\n");
      nb_destroy(nb);
      isRunning = 0; 
  }

    // If client sends one of the following: EHLO, RSET, VRFY, EXPN, HELP, then send a
    // "502" error code mentioning commands that have not been implemented
  if((strncasecmp(foo,"EHLO",4) == 0) || (strncasecmp(foo,"RSET",4) == 0) ||
   (strncasecmp(foo,"VRFY",4) == 0) || (strncasecmp(foo,"EXPN",4) == 0) ||
    (strncasecmp(foo,"HELP",4) == 0)) {
    send_string(fd,"502 Command not implemented\r\n");
  }

  switch(serverState) {

    case 0 :
    
    // If client sends "HELO", respond with "250" & the domain of the client, otherwise
    //  send a "500" error if client sends unrecognized commands or a "503" error if
    // client sends commands not in sequence
    if(strncasecmp(foo,"HELO",4) == 0) {
      printf("%s\n", foo);
      char heloArr[100];
      snprintf(heloArr,100, "HELO %s\r\n", unameData.nodename);

      if(strcasecmp(foo,heloArr) == 0) {
        send_string(fd,"250 HELO %s\r\n", unameData.nodename);
        serverState = WaitingMail;
    } 
    else {
       printbadSeq(fd);
    }
  }  else if((strncasecmp(foo,"MAIL",4) == 0) ||(strncasecmp(foo,"RCPT",4) == 0) ||
            (strncasecmp(foo,"DATA",4) == 0)) {
              printbadSeq(fd);

  } else {
    if(strncasecmp(foo,"NOOP",4) != 0 && strncasecmp(foo,"QUIT",4) != 0) {
      printbadSyn(fd);
    }
  }
    
  break;

   case 1 : 
   // If client sends "MAIL", create a user list, otherwise
   // send a "500" error if client sends unrecognized commands or a "503" error if
   // client sends commands not in sequence
   //TODO mail from:<> --> error
   if(strncasecmp(foo,"MAIL FROM:<",11) == 0) {
            char *f_char = strchr(foo,'<');
            char *e_char = strchr(foo,'>');
            int f_index = (int) (f_char-foo);
            int e_index = (int) (e_char-foo);

            f_char = &f_char[1];
            size_t len = strlen(f_char);
            f_char[len-3] = 0;

            e_char = &e_char[1];

        if(f_index < e_index && f_char[0] != 0 && e_char == 0) {

          if(ul != NULL) {
            destroy_user_list(ul);
            }
            ul = create_user_list();
            serverState = WaitingRCPT;
            send_string(fd,"250 OK\r\n");
      } else {
          printbadSeq(fd);
      }
    } 
  else if((strncasecmp(foo,"HELO",4) == 0) || (strncasecmp(foo,"MAIL",4) == 0) ||
                                              (strncasecmp(foo,"DATA",4) == 0)) {
             printbadSeq(fd);
  } 
  else {
    if(strncasecmp(foo,"NOOP",4) != 0 && strncasecmp(foo,"QUIT",4) != 0) {
            printbadSyn(fd);
    }
  }
    break;

    // if client sends "RCPT", verify if recipient is a valid user, then add them 
    // to the user list, otherwise
    // send a "500" error if client sends unrecognized commands or a "503" error if
    // client sends commands not in sequence or a "550" is no user exists
    case 2 :

    if(strncasecmp(foo,"RCPT TO:",8) == 0) {
            char *f_char = strchr(foo,'<');
            char *e_char = strchr(foo,'>');
            int f_index = (int) (f_char-foo);
            int e_index = (int) (e_char-foo);

        if(f_index < e_index) {
          char *result = strchr(foo,'<');
          result = &result[1];
          size_t len = strlen(result);
          result[len-3] = 0;

          if(is_valid_user(result,NULL) != 0) {
            add_user_to_list(&ul,result);
            serverState = WaitingRCPT;
            send_string(fd,"250 OK\r\n");

          } else {
            send_string(fd,"550 no such user %s\r\n",result);
          }

      } else {
         printbadSeq(fd);
      }

    }  
      else if((strncasecmp(foo,"HELO",4) == 0) ||(strncasecmp(foo,"MAIL",4) == 0)) {
        printbadSeq(fd);
  } 
  else {
     if(strncasecmp(foo,"NOOP",4) != 0 && strncasecmp(foo,"QUIT",4) != 0) {
            printbadSyn(fd);
    }
  }
    break;

    // if client sends "DATA", parse their message into a temporary file &
    // save its contents to every address in the user list otherwise,
    //  send a "500" error if client sends unrecognized commands or a "503" error if
    // client sends commands not in sequence
    case 3 :

    if(strncasecmp(foo,"DATA",4) == 0) {
      send_string(fd,"343 Intermediate Success\r\n");
      char fn[] = "TempFile-XXXXXX";
      int temp_mail = mkstemp(fn);
      char bar[maxSize];


      while(1){
        nb_read_line(nb,bar);
        int loo = strlen(bar);
        printf("The length of bar is: %d\r\n", loo);

        if(strcmp(&bar[strlen(bar)-2],"\r\n")) {
          send_string(fd,"500 Line too long\r\n");
          while (strcmp(&bar[strlen(bar)-2], "\r\n")){
                nb_read_line(nb, bar);
        }
      }
        if(strncasecmp(bar,".\r\n",3) == 0){
          break;
        } else {
          write(temp_mail,bar,strlen(bar));
          bar[0] = 0;
        }
      }
     
      save_user_mail(fn,ul);
      unlink(fn);
      serverState = WaitingMail;
      send_string(fd,"250 Okay\r\n");
    } 

    else if(strncasecmp(foo,"RCPT TO:",8) == 0) {
            char *f_char = strchr(foo,'<');
            char *e_char = strchr(foo,'>');
            int f_index = (int) (f_char-foo);
            int e_index = (int) (e_char-foo);

        if(f_index < e_index) {
          char *result = strchr(foo,'<');
          result = &result[1];
          size_t len = strlen(result);
          result[len-3] = 0;

          if(is_valid_user(result,NULL) != 0) {
            add_user_to_list(&ul,result);
            serverState = WaitingData;
            send_string(fd,"250 OK\r\n");

          } else {
            send_string(fd,"550 no such user %s\r\n",result);
          }

      } else {
         printbadSeq(fd);
      }

    
            } else if((strncasecmp(foo,"HELO",4) == 0) ||(strncasecmp(foo,"MAIL",4) == 0)) {
                  printbadSeq(fd); 
  } 
  else {
    if(strncasecmp(foo,"NOOP",4) != 0 && strncasecmp(foo,"QUIT",4) != 0) {
            printbadSyn(fd);
    }
  }
   break; 
   }
  }
}

void printbadSeq(int fd) {
  send_string(fd,"503 bad sequence of commands\r\n");
}
void printbadSyn(int fd) {
  send_string(fd,"500 Syntax error, command unrecognized\r\n");
}

  
  