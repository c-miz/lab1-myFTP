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
#include <cstdio>
#include <cstdint>
#include <cerrno>
#define MAXLINE 4096
#define LISTENQ 10
using namespace std;
bool connncted_ports[65536]={false};
struct Header{
    uint8_t m_protocol[6]; /* protocol magic number (6 bytes) */
    uint8_t m_type;                          /* type (1 byte) */
    uint8_t m_status;                      /* status (1 byte) */
    uint32_t m_length;                    /* length (4 bytes) in Big endian*/
    Header(uint8_t t,uint8_t s,uint32_t l):m_type(t),m_status(s),m_length(htonl(l)){
        m_protocol[0]=0xc1;
        m_protocol[1]=0xa1;
        m_protocol[2]=0x10;
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
            uint8_t type=static_cast<uint8_t>(buf[6]);
            uint8_t status=static_cast<uint8_t>(buf[7]);
            uint32_t length=ntohl(*(uint32_t*)(buf+8));
            if(type==0xA1){
                Header header(0xA2,1,12);
                ssize_t n=write(sockfd,&header,sizeof(header));
                if(n<0){
                    cerr<<"Error: Unable to send header to client"<<endl;
                }
                else{
                    cout<<"Connected to client"<<endl;
                    struct sockaddr_in peer_addr{};
                    socklen_t peer_len=sizeof(peer_addr);
                    if(getpeername(sockfd,reinterpret_cast<struct sockaddr*>(&peer_addr),&peer_len)==0){
                        connncted_ports[ntohs(peer_addr.sin_port)]=true;
                    }
                }
            }
            else if(type==0xA3){
                cout<<"Received ls command from client"<<endl;
                FILE* pipe=popen("ls", "r");
                if(pipe==nullptr){
                    cerr<<"Error: Unable to run ls"<<endl;
                    return;
                }

                string listing;
                char output[MAXLINE];
                size_t bytes_read;
                while((bytes_read=fread(output,1,sizeof(output),pipe))>0){
                    listing.append(output,bytes_read);
                }
                bool read_failed=ferror(pipe)!=0;
                int exit_status=pclose(pipe);
                if(read_failed||exit_status!=0){
                    cerr<<"Error: Unable to read ls output"<<endl;
                    return;
                }

                // LIST_REPLY includes the header and a null-terminated listing.
                Header header(0xA4,0,
                    static_cast<uint32_t>(sizeof(Header)+listing.size()+1));
                vector<char> reply(sizeof(Header)+listing.size()+1);
                memcpy(reply.data(),&header,sizeof(header));
                memcpy(reply.data()+sizeof(header),listing.c_str(),listing.size()+1);

                size_t sent=0;
                while(sent<reply.size()){
                    ssize_t bytes_sent=send(sockfd,reply.data()+sent,
                        reply.size()-sent,MSG_NOSIGNAL);
                    if(bytes_sent<0&&errno==EINTR){
                        continue;
                    }
                    if(bytes_sent<=0){
                        cerr<<"Error: Unable to send ls reply to client"<<endl;
                        return;
                    }
                    sent+=static_cast<size_t>(bytes_sent);
                }
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
        close(connfd);
        
    }
    return 0;
}