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
            else if(type==0xA7){
                cout<<"Received get command from client"<<endl;
                uint32_t length=ntohl(*(uint32_t*)(buf+8));
                size_t read_bytes=n;
                while(read_bytes<length){
                    ssize_t bytes_read=read(sockfd,buf+read_bytes,length-read_bytes);
                    if(bytes_read<0&&errno==EINTR){
                        continue;
                    }
                    else if(bytes_read<=0){
                        cerr<<"Error: Unable to read get request from client"<<endl;
                        return;
                    }
                    read_bytes+=static_cast<size_t>(bytes_read);
                }
                string filename(buf+sizeof(Header),length-sizeof(Header));
                fs::path file_path=session.cwd/filename;
                if(!fs::exists(file_path)||!fs::is_regular_file(file_path)){
                    Header header(0xA8,0,12);
                    ssize_t n=write(sockfd,&header,sizeof(header));
                    if(n<0){
                        cerr<<"Error: Unable to send get reply to client"<<endl;
                    }
                    return;
                }
                else{
                    Header header(0xA8,1,12);
                    ssize_t n=write(sockfd,&header,sizeof(header));
                    if(n<0){
                        cerr<<"Error: Unable to send get reply to client"<<endl;
                    }
                    else{
                        FILE* file=fopen(file_path.c_str(),"rb");
                        if(file==nullptr){
                            Header header(0xFF,0,12);
                            ssize_t n=write(sockfd,&header,sizeof(header));
                            if(n<0){
                                cerr<<"Error: Unable to send header to client"<<endl;
                            }
                        }
                        else{
                            char buf[MAXLINE];
                            size_t n;
                            Header header(0xFF,1,12+static_cast<uint32_t>(fs::file_size(file_path)));
                            ssize_t n=write(sockfd,&header,sizeof(header));
                            while((n=fread(buf,1,sizeof(buf),file))>0){
                                size_t sent=0;
                                while(sent<n){
                                    ssize_t bytes_sent=send(sockfd,buf+sent,n-sent,MSG_NOSIGNAL);
                                    if(bytes_sent<0&&errno==EINTR){
                                        continue;
                                    }
                                    else if(bytes_sent<=0){
                                        cerr<<"Error: Unable to send file to client"<<endl;
                                        fclose(file);
                                        return;
                                    }
                                    sent+=static_cast<size_t>(bytes_sent);
                                }
                            }
                        }
                        fclose(file);
                    }
                }
            }
            else if(type==0xA9){
                static_cast<uint32_t>length=ntohl(*(uint32_t*)(buf+8));
                size_t read_bytes=n;
                while(read_bytes<length){
                    ssize_t bytes_read=read(sockfd,buf+read_bytes,length-read_bytes);
                    if(bytes_read<0&&errno==EINTR){
                        continue;
                    }
                    if(bytes_read<=0){
                        cerr<<"Error: Unable to read file from client"<<endl;
                        return;
                    }
                    read_bytes+=static_cast<size_t>(bytes_read);
                }
                string filename(buf+sizeof(Header),length-sizeof(Header));
                fs::path file_path=session.cwd/filename;
                Header header(0xAA,0,12);
                ssize_t n=write(sockfd,&header,sizeof(header));
                if(n<0){
                    cerr<<"Error: Unable to send put reply to client"<<endl;
                }
                n=read(sockfd,buf,MAXLINE);
                if(n<0){
                    cerr<<"Error: Unable to read header from client"<<endl;
                    return;
                }
                if(checkHeader(buf)&&buf[6]==0xFF){
                    uint32_t length=ntohl(*(uint32_t*)(buf+8));
                    FILE* file=fopen(file_path.c_str(),"wb");
                    if(file==nullptr){
                        cerr<<"Error: Unable to open file "<<filename<<" for writing"<<endl;
                        return;
                    }
                    size_t received=0;
                    while(received<length){
                        ssize_t bytes_read=read(sockfd,buf,MAXLINE);
                        if(bytes_read<0&&errno==EINTR){
                            continue;
                        }
                        else if(bytes_read<=0){
                            cerr<<"Error: Unable to read file from client"<<endl;
                            fclose(file);
                            return;
                        }
                        size_t bytes_written=fwrite(buf,1,static_cast<size_t>(bytes_read),file);
                        if(bytes_written<static_cast<size_t>(bytes_read)){
                            cerr<<"Error: Unable to write to file "<<filename<<endl;
                            fclose(file);
                            return;
                        }
                        received+=static_cast<size_t>(bytes_read);
                    }
                    fclose(file);
                }
                else{
                    cerr<<"Error: Invalid header received"<<endl;
                }
            }
            else if(type==0xAB){
                static_cast<uint32_t>length=ntohl(*(uint32_t*)(buf+8));
                size_t read_bytes=n;
                while(read_bytes<length){
                    ssize_t bytes_read=read(sockfd,buf+read_bytes,length-read_bytes);
                    if(bytes_read<0&&errno==EINTR){
                        continue;
                    }
                    if(bytes_read<=0){
                        cerr<<"Error: Unable to read file from client"<<endl;
                        return;
                    }
                    read_bytes+=static_cast<size_t>(bytes_read);
                }
                string filename(buf+sizeof(Header),length-sizeof(Header));
                fs::path file_path=session.cwd/filename;
                if(!fs::exists(file_path)||!fs::is_regular_file(file_path)){
                    Header header(0xAC,0,12);
                    ssize_t n=write(sockfd,&header,sizeof(header));
                    if(n<0){
                        cerr<<"Error: Unable to send sha256 reply to client"<<endl;
                    }
                    return;
                }
                else{
                    Header header(0xAC,1,12);
                    ssize_t n=write(sockfd,&header,sizeof(header));
                    if(n<0){
                        cerr<<"Error: Unable to send sha256 reply to client"<<endl;
                    }
                    else{
                        string command="sha256sum " + file_path.string();
                        FILE* pipe=popen(command.c_str(),"r");
                        if(pipe==nullptr){
                            cerr<<"Error: Unable to run sha256sum"<<endl;
                            return;
                        }
                        string hash;
                        char output[MAXLINE];
                        size_t bytes_read;
                        while((bytes_read=fread(output,1,sizeof(output),pipe))>0){
                            hash.append(output,bytes_read);
                        }
                        pclose(pipe);
                        Header header(0xFF,0,12+static_cast<uint32_t>(hash.size()+1));
                        vector<char> reply(sizeof(Header)+hash.size()+1);
                        memcpy(reply.data(),&header,sizeof(header));
                        memcpy(reply.data()+sizeof(Header),hash.c_str(),hash.size()+1);
                        size_t sent=0;
                        while(sent<reply.size()){
                            ssize_t bytes_sent=send(sockfd,reply.data()+sent,
                                reply.size()-sent,MSG_NOSIGNAL);
                            if(bytes_sent<0&&errno==EINTR){
                                continue;
                            }
                            if(bytes_sent<=0){
                                cerr<<"Error: Unable to send reply to client"<<endl;
                                return;
                            }
                            sent+=static_cast<size_t>(bytes_sent);
                        }
                    }
                }
            }
            else if(type==0xAD){
                cout<<"Received quit command from client"<<endl;
                Header header(0xAE,0,12);
                ssize_t n=write(sockfd,&header,sizeof(header));
                if(n<0){
                    cerr<<"Error: Unable to send quit reply to client"<<endl;
                }
                return;
            }
            else{
                cerr<<"Error: Unknown command received"<<endl;
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