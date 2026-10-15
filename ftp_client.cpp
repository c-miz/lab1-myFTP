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
#define MAXLINE 4096

using namespace std;
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
struct sockaddr_in server_addr;
class Client{
    private:
    bool isConnected;
    int sockfd;
    public:
    void open(string ip,int port){
        sockfd=socket(AF_INET,SOCK_STREAM,0);
        bzero(&server_addr,sizeof(server_addr));
        server_addr.sin_family=AF_INET;
        server_addr.sin_port=htons(port);
        inet_pton(AF_INET,ip.c_str(),&server_addr.sin_addr);
        int result=connect(sockfd,(struct sockaddr*)&server_addr,sizeof(server_addr));
        if(result<0){
            cerr<<"Error: Unable to connect to server"<<endl;
            return; }
        else{
            cout<<"Connected to server "<<ip<<":"<<port<<endl;
        }
        Header header(0xA1,0,12);
        ssize_t n=write(sockfd,&header,sizeof(header));
        if(n<0){
            cerr<<"Error: Unable to send header to server"<<endl;
            return;
        }
        ssize_t n;
        char buf[MAXLINE];
        n=read(sockfd,buf,MAXLINE);
        if(n<0){
            cerr<<"Error: Unable to read header from server"<<endl;
            return;
        }
        if(checkHeader(buf)){
            type type=buf[6];
            status status=buf[7];
            uint32_t length=ntohl(*(uint32_t*)(buf+8));
            if(type==0xA2&&status==1){
                cout<<"Connected to server"<<endl;
                isConnected=true;
            }
                if(n<0){
                    cerr<<"Error: Unable to send header to client"<<endl;
                }

            }
        else{
            cerr<<"Error: Invalid header received"<<endl;
        }
        return;
    }
    void ls(){
        if(!isConnected){
            cerr<<"Error: Not connected to server"<<endl;
            return;
        }
        Header header(0xA3,0,12);
        ssize_t n=write(sockfd,&header,sizeof(header));
        if(n<0){
            cerr<<"Error: Unable to send header to server"<<endl;
            return;
        }
        char buf[MAXLINE];
        n=read(sockfd,buf,MAXLINE);
        if(n<0){
            cerr<<"Error: Unable to read header from server"<<endl;
            return;
        }
        if(checkHeader(buf)&&buf[6]==0xA4){
            uint32_t length=ntohl(*(uint32_t*)(buf+8));
            if(length>12){
                string listing=NULL;
                while(listing.size()<length-12){
                    n=read(sockfd,buf,MAXLINE);
                    if(bytes_sent<0&&errno==EINTR){
                        continue;
                    }
                    else if(n<=0){
                        cerr<<"Error: Unable to read listing from server"<<endl;
                        return;
                    }
                    listing.append(buf,n);
                }
            }
            else{
                cout<<"No files found"<<endl;
            }
        }
        else{
            cerr<<"Error: Invalid header received"<<endl;
        }
    }
    void cd(string directory){
         if(!isConnected){
            cerr<<"Error: Not connected to server"<<endl;
            return;
        }
        Header header(0xA5,0,static_cast<uint32_t>(sizeof(Header)+directory.size()+1));
        vector<char> request(sizeof(Header)+directory.size()+1);
        memcpy(request.data(),&header,sizeof(header));
        memcpy(request.data()+sizeof(header),directory.c_str(),directory.size()+1);
        uint32_t sent=0;
        while(sent<request.size()){
            size_t bytes_sent=send(sockfd,request.data()+sent,
                request.size()-sent,MSG_NOSIGNAL);
            if(bytes_sent<0&&errno==EINTR){
                continue;  
            }
            else if(bytes_sent<=0){
                cerr<<"Error: Unable to send request to server"<<endl;
                return;
            }
            sent+=bytes_sent;
        }
        result=read(sockfd,buf,MAXLINE);
        if(n<0){
            cerr<<"Error: Unable to read header from server"<<endl;
            return;
        }
        if(checkHeader(buf)&&buf[6]==0xA6){
            status status=buf[7];
            if(status==1){
                cout<<"Changed directory to "<<directory<<endl;
            }
            else{
                cerr<<"directory does not exist:"<<directory<<endl;
                return;
            }
        }
        else{
            cerr<<"Error: Invalid header received"<<endl;
        }
    }
    void get(string filename){
        if(!isConnected){
            cerr<<"Error: Not connected to server"<<endl;
            return;
        }
        Header header(0xA7,0,static_cast<uint32_t>(sizeof(Header)+filename.size()+1));
        vector<char> request(sizeof(Header)+filename.size()+1);
        memcpy(request.data(),&header,sizeof(header));
        memcpy(request.data()+sizeof(header),filename.c_str(),filename.size()+1);
        uint32_t sent=0;
        while(sent<request.size()){
            size_t bytes_sent=send(sockfd,request.data()+sent,
                request.size()-sent,MSG_NOSIGNAL);
            if(bytes_sent<0&&errno==EINTR){
                continue;  
            }
            else if(bytes_sent<=0){
                cerr<<"Error: Unable to send request to server"<<endl;
                return;
            }
            sent+=bytes_sent;
        }
        s_size_t n=read(sockfd,buf,MAXLINE);
        if(n<0){
            cerr<<"Error: Unable to read header from server"<<endl;
            return;
        }
        if(checkHeader(buf)&&buf[6]==0xA8){
            status status=buf[7];
            if(status==0){
                cerr<<"Error: File "<<filename<<" does not exist on server"<<endl;
            }
            else{
                n=read(sockfd,buf,MAXLINE);
                if(n<0){
                    cerr<<"Error: Unable to read file from server"<<endl;
                    return;
                }
                if(checkHeader(buf)&&buf[6]==0xFF){
                    static_cast<uint32_t>file_size=ntohl(*(uint32_t*)(buf+8))-12;
                    FILE* file=fopen(filename.c_str(),"wb");
                    if(file==nullptr){
                        cerr<<"Error: Unable to open file "<<filename<<" for writing"<<endl;
                        return;
                    }
                    size_t received=0;
                    while(received<file_size){
                        n=read(sockfd,buf,MAXLINE);
                        if(n<0&&errno==EINTR){
                            continue;
                        }
                        if(n<=0){
                            cerr<<"Error: Unable to read file from server"<<endl;
                            fclose(file);
                            return;
                        }
                        fwrite(buf,1,n,file);
                        received+=n;
                    }
                    fclose(file);
                }
            }
        }
        else{
            cerr<<"Error: Invalid header received"<<endl;
        }
    }
    void put(string filename){
        if(!isConnected){
            cerr<<"Error: Not connected to server"<<endl;
            return;
        }
        if(fs:exists(filename)&&fs::is_regular_file(filename)){
            Header header(0xA9,0,12+static_cast<uint32_t>size_of(filename)+1);
            vector<char> request(sizeof(Header)+filename.size()+1);
            memcpy(request.data(),&header,sizeof(header));
            memcpy(request.data()+sizeof(header),filename.c_str(),filename.size()+1);
            uint32_t sent=0;
            while(sent<request.size()){
                size_t bytes_sent=send(sockfd,request.data()+sent,
                    request.size()-sent,MSG_NOSIGNAL);
                if(bytes_sent<0&&errno==EINTR){
                    continue;  
                }
                else if(bytes_sent<=0){
                    cerr<<"Error: Unable to send request to server"<<endl;
                    return;
                }
                sent+=bytes_sent;
            }
            ssize_t n=read(sockfd,buf,MAXLINE);
            if(n<0){
                cerr<<"Error: Unable to read header from server"<<endl;
                return;
            }
            if(checkHeader(buf)&&buf[6]==0xAA){
                    fs::path file_path=fs::absolute(filename);
                    FILE* file=fopen(file_path.c_str(),"rb");
                    if(file==nullptr){
                        cerr<<"Error: Unable to open file "<<filename<<" for reading"<<endl;
                        return;
                    }
                    Header header(0xFF,0,12+static_cast<uint32_t>(fs::file_size(file_path)));
                    ssize_t n=write(sockfd,&header,sizeof(header));
                    if(n<0){
                        cerr<<"Error: Unable to send header to server"<<endl;
                        fclose(file);
                        return;
                    }
                    char buf[MAXLINE];
                    size_t n;
                    while((n=fread(buf,1,sizeof(buf),file))>0){
                        size_t sent=0;
                        while(sent<n){
                            ssize_t bytes_sent=send(sockfd,buf+sent,n-sent,MSG_NOSIGNAL);
                            if(bytes_sent<0&&errno==EINTR){
                                continue;
                            }
                            else if(bytes_sent<=0){
                                cerr<<"Error: Unable to send file to server"<<endl;
                                fclose(file);
                                return;
                            }
                            sent+=static_cast<size_t>(bytes_sent);
                        }
                    }
                    fclose(file);
            }

        }
        else{
            cerr<<"Error: File "<<filename<<" does not exist"<<endl;
            return;
        }
    }
    void sha256(string filename){
        if(!isConnected){
            cerr<<"Error: Not connected to server"<<endl;
            return;
        }
        Header header(0xAB,0,static_cast<uint32_t>(sizeof(Header)+filename.size()+1));
        vector<char> request(sizeof(Header)+filename.size()+1);
        memcpy(request.data(),&header,sizeof(header));
        memcpy(request.data()+sizeof(header),filename.c_str(),filename.size()+1);
        uint32_t sent=0;
        while(sent<request.size()){
            size_t bytes_sent=send(sockfd,request.data()+sent,
                request.size()-sent,MSG_NOSIGNAL);
            if(bytes_sent<0&&errno==EINTR){
                continue;  
            }
            else if(bytes_sent<=0){
                cerr<<"Error: Unable to send request to server"<<endl;
                return;
            }
            sent+=bytes_sent;
        }
        ssize_t n=read(sockfd,buf,MAXLINE);
        if(n<0){
            cerr<<"Error: Unable to read header from server"<<endl;
            return;
        }
        if(checkHeader(buf)&&buf[6]==0xAC){
            status status=buf[7];
            if(status==0){
                cerr<<"Error: File "<<filename<<" does not exist on server"<<endl;
                return;
            }
            else{
                n=read(sockfd,buf,MAXLINE);
                if(n<0){
                    cerr<<"Error: Unable to read hash from server"<<endl;
                    return;
                }
                if(checkHeader(buf)&&buf[6]==0xFF){
                    uint32_t length=ntohl(*(uint32_t*)(buf+8));
                    string hash(buf+sizeof(Header),length-sizeof(Header));
                    cout<<hash<<endl;
                }
                else{
                    cerr<<"Error: Invalid header received"<<endl;
                }
            }
        }
        else{
            cerr<<"Error: Invalid header received"<<endl;
        }
    }
    bool quit(){
        if(isConnected){
            Header header(0xAD,0,12);
            ssize_t n=write(sockfd,&header,sizeof(header));
            if(n<0){
                cerr<<"Error: Unable to send header to server"<<endl;
            }
            n=read(sockfd,buf,MAXLINE);
            if(n<0){
                cerr<<"Error: Unable to read header from server"<<endl;
                return;
            }
            if(checkHeader(buf)&&buf[6]==0xAE){
                cout<<"Disconnected from server"<<endl;
                close(sockfd);
                isConnected=false;
                return 0;
            }
            else{
                cerr<<"Error: Invalid header received"<<endl;
            }
        }
        else{
            return 1;
        }
    }

};
vector<string> parseCommand(const string& line) {
    vector<string> arguments;
    istringstream input(line);
    string argument;

    while (input >> quoted(argument)) {
        arguments.push_back(argument);
    }

    return arguments;
}

int main() {
    Client client;
    string line;
    while (true) {
        cout << "ftp> ";
        getline(cin, line);
        vector<string> arguments=parseCommand(line);
        if(arguments.empty()){
            continue;
        }
        string command=arguments[0];
        if(command=="open"){
            if(arguments.size()!=3){
                cout<<"Usage: open <ip> <port>"<<endl;
                continue;
            }
            string ip=arguments[1];
            int port=stoi(arguments[2]);
            client.open(ip,port);
        }
        else if(command=="ls"){
            client.ls();
        }
        else if(command=="cd"){
            if(arguments.size()!=2){
                cout<<"Usage: cd <directory>"<<endl;
                continue;
            }
            string directory=arguments[1];
            client.cd(directory);
        }
        else if(command=="get"){
            if(arguments.size()!=2){
                cout<<"Usage: get <filename>"<<endl;
                continue;
            }
            string filename=arguments[1];
            client.get(filename);
        }
        else if(command=="put"){
            if(arguments.size()!=2){
                cout<<"Usage: put <filename>"<<endl;
                continue;
            }
            string filename=arguments[1];
            client.put(filename);
        }
        else if(command=="sha256"){
            if(arguments.size()!=2){
                cout<<"Usage: sha256 <filename>"<<endl;
                continue;
            }
            string filename=arguments[1];
            client.sha256(filename);
        }
        else if(command=="quit"){
            if(client.quit()){
                break;
            }
        }
        else{
            cout<<"Unknown command: "<<command<<endl;
        }
    }
    return 0;
}