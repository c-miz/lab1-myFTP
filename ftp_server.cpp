#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <sys/socket.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>
#include <arpa/inet.h>
#include <studio.h>
#define MAXLINE 4096
#define LISTENQ 10
using namespace std;
bool connncted_ports[65536]={false};
struct Header{
    byte m_protocol[6]; /* protocol magic number (6 bytes) */
    type m_type;                          /* type (1 byte) */
    status m_status;                      /* status (1 byte) */
    uint32_t m_length;                    /* length (4 bytes) in Big endian*/
    Header(type t,status s,uint32_t l):m_type(t),m_status(s),m_length(l){
        m_protocol[0]=\xc1;
        m_protocol[1]=\xa1;
        m_protocol[2]=\x10;
        m_protocol[3]='f';
        m_protocol[4]='t';
        m_protocol[5]='p';
    }                   
}__attribute__ ((packed));
bool checkHeader(const char* buf){
    return buf[0]=='\xc1'&&buf[1]=='\xa1'&&buf[2]=='\x10'&&buf[3]=='f'&&buf[4]=='t'&&buf[5]=='p';
}
void str_echo(int sockfd){
    ssize_t n;
    char buf[MAXLINE];
again:
    while((n=read(sockfd,buf,MAXLINE))>0){
        if(checkHeader(buf)){
            type type=buf[6];
            status status=buf[7];
            uint32_t length=ntohl(*(uint32_t*)(buf+8));
            if(type==0xA1){
                Header header(0xA2,1,12);
                ssize_t n=write(sockfd,&header,sizeof(header));
                if(n<0){
                    cerr<<"Error: Unable to send header to client"<<endl;
                }
                else{
                    cout<<"Connected to client"<<endl;
                    connncted_ports[ntohs(((struct sockaddr_in*)&sockfd)->sin_port)]=true;
                }
            }
            else if(type==0xA3){
                cout<<"Received ls command from client"<<endl;
                
            }

            

        }
        else{
            cerr<<"Error: Invalid header received"<<endl;
        }
    }
    if(n<0&&errno==EINTR)
        goto again;
    else if(n<0){
        cerr<<"Error: read error"<<endl;
    }
}
int main(int argc, char **argv) {
    string IP_ADDRESS;
    int SERVER_PORT;
    if(argc!=3){
        IP_ADDRESS="127.0.0.1";
        SERVER_PORT=12323;
    }
    else{
        IP_ADDRESS=argv[1];
        SERVER_PORT=stoi(argv[2]);
    }
    int listenfd, connfd;
    socklen_t clilen;
    struct sockaddr_in cliaddr, servaddr;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=inet_addr(IP_ADDRESS.c_str());
    servaddr.sin_port = htons(SERVER_PORT);
    bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    listen(listenfd, LISTENQ);

    while(true){
        clilen=sizeof(cliaddr);
        connfd=accept(listenfd,(struct sockaddr*)&cliaddr,&clilen);
        str_echo(connfd);
        
    }
    return 0;
}