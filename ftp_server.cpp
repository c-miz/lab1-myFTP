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
#include <filesystem>
#include <system_error>
#define MAXLINE 4096
#define LISTENQ 10
namespace fs = std::filesystem;
using namespace std;
bool connncted_ports[65536]={false};
struct ClientSession {
    fs::path cwd;  // 当前目录，保存为绝对路径
};
bool changeDirectory(ClientSession& session, const std::string& dir) {
    // 按实验要求，目录名只能包含英文字母和数字
    if (dir.empty() ||
        dir.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789") != std::string::npos) {
        return false;
    }

    // 相对于该客户端的当前目录解析
    fs::path target = session.cwd / dir;
    std::error_code ec;

    // 不存在、不是目录或检查失败，均返回 false
    if (!fs::is_directory(target, ec) || ec) {
        return false;
    }

    // 转换为规范的绝对路径；失败时不修改原来的 cwd
    fs::path resolved = fs::canonical(target, ec);
    if (ec) {
        return false;
    }

    session.cwd = resolved;
    return true;
}
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
void str_echo(int sockfd, const fs::path& initial_dir) {
    ssize_t n;
    char buf[MAXLINE];
    ClientSession session{initial_dir};
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
                FILE* pipe=popen(("ls " + session.cwd.string()).c_str(), "r");
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
            else if(type==0xA5){
                cout<<"Received cd command from client"<<endl;
                uint32_t length=ntohl(*(uint32_t*)(buf+8));
                size_t read_bytes=n;
                while(read_bytes<length){
                    ssize_t bytes_read=read(sockfd,buf+read_bytes,length-read_bytes);
                    if(bytes_read<0&&errno==EINTR){
                        continue;
                    }
                    else if(bytes_read<=0){
                        cerr<<"Error: Unable to read cd request from client"<<endl;
                        return;
                    }
                    read_bytes+=static_cast<size_t>(bytes_read);
                }
                string directory(buf+sizeof(Header),length-sizeof(Header));
                bool success=changeDirectory(session,directory);
                Header header(0xA6,success?1:0,12);
                ssize_t n=write(sockfd,&header,sizeof(header));
                if(n<0){
                    cerr<<"Error: Unable to send cd reply to client"<<endl;
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
    const fs::path initial_dir = fs::current_path();
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
        str_echo(connfd, initial_dir);
        close(connfd);
        
    }
    return 0;
}