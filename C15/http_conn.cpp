//
// Created by 谭演锋 on 2024/4/18.
//
#include"http_conn.h"
/*定义HTTP响应的一些状态信息*/
const char*ok_200_title="OK";
const char*error_400_title="Bad Request";
const char*error_400_form="Your request has bad syntax or is inherently impossible to satisfy.\n";
const char*error_403_title="Forbidden";
const char*error_403_form="You do not have permission to get file from this server.\n";
const char*error_404_title="Not Found";
const char*error_404_form="The requested file was not found on this server.\n";
const char*error_500_title="Internal Error";
const char*error_500_form="There was an unusual problem serving the requested file.\n";
/*网站的根目录*/
const char*doc_root="/var/www/html";
int setnonblocking(int fd)
{
    int old_option=fcntl(fd,F_GETFL);
    int new_option=old_option|O_NONBLOCK; fcntl(fd,F_SETFL,new_option);
    return old_option;
}
void addfd(int epollfd,int fd,bool one_shot) {
    epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN|EPOLLET|EPOLLRDHUP;
    if(one_shot)
    {
        event.events|=EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}
void removefd(int epollfd,int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}
void modfd(int epollfd,int fd,int ev)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=ev|EPOLLET|EPOLLONESHOT|EPOLLRDHUP;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}
int http_conn::m_user_count=0;
int http_conn::m_epollfd=-1;
void http_conn::close_conn(bool real_close)
{
    if(real_close&&(m_sockfd!=-1))
    {
        removefd(m_epollfd,m_sockfd);
        m_sockfd=-1;
        m_user_count--;
        /*关闭一个连接时，将客户总量减1*/
    }
}
void http_conn::init(int sockfd,const sockaddr_in&addr)
{
    m_sockfd=sockfd;
    m_address=addr;
    /*如下两行是为了避免TIME_WAIT状态，仅用于调试，实际使用时应该去掉*/
    int reuse=1;
    /*setsockopt() 函数用于设置套接字选项。在你提供的代码中，它被用于设置 SO_REUSEADDR(重用本地地址) 选项，
     * 这个选项告诉内核允许重新绑定一个已经在使用中的地址
     * euse 被设置为 1，这表示要启用 SO_REUSEADDR 选项。这意味着在绑定套接字时允许地址的重用。
     * 这对于服务器进程在终止后立即重新启动并尝试在同一地址上绑定套接字是很有用的。
     * 通常情况下，如果之前绑定的套接字在关闭后立即重新启动服务器，会导致地址被占用的错误。
     * 原文：经过setsockopt的设置之后，即使sock处于TIME_WAIT状态，与之绑定的socket地址也可以立即被重用。
     * 此外，我们也可以通过修改内核参数/proc/sys/net/ipv4/tcp_tw_recycle来快速回收被关闭的socket，
     * 从而使得TCP连接根本就不进入TIME_WAIT状态，进而允许应用程序立即重用本地的socket地址。
     * int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
     *  sockfd：套接字描述符。
        level：选项所在的协议层，通常为 SOL_SOCKET。
        optname：要设置的选项名，这里是 SO_REUSEADDR。
        optval：指向包含选项值的缓冲区的指针。
        optlen：选项值的长度。*/
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    addfd(m_epollfd,sockfd,true);
    m_user_count++;
//    调用无参初始化函数，初始化所有数据
    init();
}
void http_conn::init()
{
    m_check_state=CHECK_STATE_REQUESTLINE;
    m_linger=false;
    m_method=GET;
    m_url=0;
    m_version=0;
    m_content_length=0;
    m_host=0;
    m_start_line=0;
    m_checked_idx=0;
    m_read_idx=0;
    m_write_idx=0;
    memset(m_read_buf,'\0',READ_BUFFER_SIZE);
    memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
    memset(m_real_file,'\0',FILENAME_LEN);
}
/*从状态机，其分析请参考8.6节，"ReadHttp.cpp"这里不再赘述*/
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for(;m_checked_idx<m_read_idx;++m_checked_idx)
    {
        temp=m_read_buf[m_checked_idx];
        if(temp=='\r')
        {
            if((m_checked_idx+1)==m_read_idx)
            {
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_idx+1]=='\n')
            {
                m_read_buf[m_checked_idx++]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        /*如果当前的字节是“\n”，即换行符，则也说明可能读取到一个完整的行*/
        else if(temp=='\n')
        {
            if((m_checked_idx>1)&&(m_read_buf[m_checked_idx-1]=='\r'))
            {
                m_read_buf[m_checked_idx-1]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}
/*循环读取客户数据，直到无数据可读或者对方关闭连接*/
bool http_conn::read()
{
/*m_read_idx标识读缓冲中已经读入的客户数据的最后一个字节的下一个位置*/
    if(m_read_idx>=READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read=0;
    while(true)
    {
        bytes_read=recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
        if(bytes_read==-1)
        {
            /*`EAGAIN` 和 `EWOULDBLOCK` 是两个常见的错误码，通常用于非阻塞 I/O 操作的返回值。
            - `EAGAIN` 表示操作暂时无法完成，并且应该重试。在非阻塞 I/O 中，它通常表示暂时没有数据可读取或者暂时无法写入数据。
            - `EWOULDBLOCK` 与 `EAGAIN` 具有相同的语义，它是 POSIX 标准下对 `EAGAIN` 的另一个名称。在某些系统中，`EAGAIN` 和 `EWOULDBLOCK` 可能会被用来表示相同的情况，但在其他系统中可能会有一些微妙的区别。
                         * */
            if(errno==EAGAIN||errno==EWOULDBLOCK)
            {
                break;
            }
            return false;
        }
        else if(bytes_read==0)
        {
            return false;
        }
        m_read_idx+=bytes_read;
    }
    return true;
}
/*解析HTTP请求行，获得请求方法、目标URL，以及HTTP版本号
 * 请求行GET / HTTP/1.1*/
http_conn::HTTP_CODE http_conn::parse_request_line(char*text) {
//找\t
/*`\t` 是 C/C++ 中的转义字符，表示水平制表符（Tab）。当输出字符串中包含 `\t` 时，
 * 它会在该位置插入一个水平制表符
 * 通常会导致输出的文本在该位置向右对齐到下一个制表位。水平制表符的宽度通常是固定的，
 * 通常相当于打印机或终端的当前制表位列位置到下一个固定列位置之间的空间大小。
 * 最后返回指向\t的指针
 * m_url客户请求的目标文件的文件名*/
    m_url=strpbrk(text,"\t");
    if(!m_url)
    {
        return BAD_REQUEST;
    }
//    加入\0会自动切断字符串\0表示字符串的结束
    *m_url++='\0';
    char*method=text;
    if(strcasecmp(method,"GET")==0)
    {
        m_method=GET;
    }
    else
    {
        return BAD_REQUEST;
    }
    /*这行代码使用 `strspn` 函数来跳过 `m_url` 字符串开头的空白字符（包括空格和制表符 `\t`）。
     * `strspn` 函数用于计算字符串中连续的字符中有多少个在指定字符集合中。
     * 在这里，它用于计算 `m_url` 字符串开头的空白字符的数量，
     * 然后将 `m_url` 指针向前移动相应的位置，以便后续处理不包含空白字符的 URL 部分。*/
    m_url+=strspn(m_url,"\t");
    m_version=strpbrk(m_url,"\t");
    if(!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++='\0';
    m_version+=strspn(m_version,"\t");
    if(strcasecmp(m_version,"HTTP/1.1")!=0)
    {
        return BAD_REQUEST;
    }
    if(strncasecmp(m_url,"http://",7)==0)
    {
        m_url+=7;
        m_url=strchr(m_url,'/');
    }
    if(!m_url||m_url[0]!='/')
    {
        return BAD_REQUEST;
    }
    m_check_state=CHECK_STATE_HEADER;
    return NO_REQUEST;
}
/*解析HTTP请求的一个头部信息*/
http_conn::HTTP_CODE http_conn::parse_headers(char*text)
{
/*遇到空行，表示头部字段解析完毕*/
    if(text[0]=='\0')
    { /*如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
 *      状态机转移到CHECK_STATE_CONTENT状态*/
        if(m_content_length!=0)
        {
            m_check_state=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
    /*否则说明我们已经得到了一个完整的HTTP请求*/
        return GET_REQUEST;
    }
/*处理Connection头部字段*/
    else if(strncasecmp(text,"Connection:",11)==0)
    {
        text+=11;
        text+=strspn(text,"\t");
        if(strcasecmp(text,"keep-alive")==0)
        {
            m_linger=true;
        }
    }
        /*处理Content-Length头部字段  请求体
         * 表示传输的请求／响应的Body的长度。GET请求因为没有Body，所以不需要这个头。携带Body的并且可以提前知道Body长度的请求
         * ／响应必须带上这个字段，以便对方可以方便的分辨出报文的边界，也就是Body数据何时结束。如果Body太大，需要边计算边传输，
         * 不到最后计算结束是无法知道整个Body大小的，这个时候可以使用http分块传输，这个时候也是不需要Content-Length字段的。*/
    else if(strncasecmp(text,"Content-Length:",15)==0)
    {
        text+=15;
        text+=strspn(text,"\t");
//        把字符传专程长整形
        m_content_length=atol(text);
    }
/*处理Host头部字段*/
    else if(strncasecmp(text,"Host:",5)==0)
    {
        text+=5;
        text+=strspn(text,"\t");
        m_host=text;
    }
    else
    {
        printf("oop!unknow header%s\n",text);
    }
    return NO_REQUEST;
}
/*我们没有真正解析HTTP请求的消息体，只是判断它是否被完整地读入了*/
http_conn::HTTP_CODE http_conn::parse_content(char*text) {
    if(m_read_idx>=(m_content_length+m_checked_idx))
    {
        text[m_content_length]='\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}
/*主状态机。其分析请参考8.6节，这里不再赘述*/
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status=LINE_OK;
    HTTP_CODE ret=NO_REQUEST;
    char*text=0;
//    CHECK_STATE_CONTENT 检查请求体 LINE_OK 读完一行
    while(((m_check_state==CHECK_STATE_CONTENT)&&(line_status==LINE_OK))||((line_status=parse_line())==LINE_OK))
    {
//        get_line() 函数可能是用来从输入中读取一行文本的函数。它的作用是从输入流中读取一行数据
        text=get_line();
        m_start_line=m_checked_idx;
        printf("got 1 http line:%s\n",text);
//        m_check_state一开始是CHECK_STATE_REQUESTLINE
        switch(m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret=parse_request_line(text);
                if(ret==BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret=parse_headers(text);
                if(ret==BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if(ret==GET_REQUEST)
                {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret=parse_content(text);
                if(ret==GET_REQUEST)
                {
                    return do_request();
                }
//                LINE_OPEN说明行还没有读完
                line_status=LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}
/*当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性。如果目标文件存
在、对所有用户可读，且不是目录，则使用mmap将其映射到内存地址m_file_address 处，并告诉调用者获取文件成功*/
http_conn::HTTP_CODE http_conn::do_request()
{
//    使用 strcpy() 函数将服务器的文档根目录路径 doc_root 复制到 m_real_file 数组中。这个数组用于存储最终的文件路径
/*strcpy()：
原型：char *strcpy(char *dest, const char *src);
功能：将源字符串 src 复制到目标字符串 dest 中，直到遇到空字符 \0 为止。
特点：不会检查目标字符串 dest 的长度，因此可能会发生缓冲区溢出的情况，如果源字符串 src 的长度超过了目标字符串 dest 的长度，可能会导致未定义的行为*/
    strcpy(m_real_file,doc_root);
    int len=strlen(doc_root);
//    使用 strncpy() 函数将 URL 路径 m_url 连接到文档根目录路径后面，形成完整的文件路径。需要注意的是，
//    为了避免溢出，这里指定了最大复制的字符数为 FILENAME_LEN - len - 1，其中 FILENAME_LEN 是 m_real_file 数组的长度。
//    这样做可以确保不会超出 m_real_file 数组的范围。
/*strncpy()：
原型：char *strncpy(char *dest, const char *src, size_t n);
功能：将源字符串 src 的前 n 个字符复制到目标字符串 dest 中，如果 src 的长度小于 n，则用空字符 \0 填充 dest 的剩余部分。
特点：提供了长度控制的功能，可以避免缓冲区溢出的问题。但是需要注意的是，如果 src 的长度大于 n，则 dest 不会被自动以空字符 \0 结束，
 因此需要手动添加 \0 以确保字符串的正确结束*/
    strncpy(m_real_file+len,m_url,FILENAME_LEN-len-1);
    /*stat 函数用于获取指定文件的状态信息，并将其保存在一个结构体中。它的原型如下：
     * int stat(const char *pathname, struct stat *statbuf)
     * pathname 参数是一个字符串，表示要获取状态信息的文件路径。
        statbuf 参数是一个指向 struct stat 类型的指针，用于保存获取到的文件状态信息。
        成功调用 stat 函数后，会将文件的各种属性信息保存在 statbuf 所指向的结构体中，
        其中包括文件大小、文件类型、访问权限等。struct stat 结构体的定义在 <sys/stat.h> 头文件中，通常包含以下成员：
        st_mode：文件的类型和权限信息。
        st_ino：文件的 inode 号，用于唯一标识一个文件。
        st_dev：文件所在的设备号。
        st_nlink：文件的硬链接数。
        st_uid：文件的所有者用户 ID。
        st_gid：文件的所有者用户组 ID。
        st_size：文件的大小（以字节为单位）。
        st_atime：文件的最后访问时间。
        st_mtime：文件的最后修改时间。
        st_ctime：文件的状态修改时间（例如权限变更）;
        stat(m_real_file, &m_file_stat) 函数用于获取文件 m_real_file 的状态信息，并将其保存在 m_file_stat 结构体中。
        如果该函数返回值小于 0，说明获取文件状态信息失败，可能是因为文件不存在或者出现了其他错误，此时返回 NO_RESOURCE，表示未找到资源。
        检查文件是否具有其他用户的读权限：通过 m_file_stat.st_mode & S_IROTH 来判断文件的权限位中是否包含其他用户的读权限。
        如果不具备读权限，则返回 FORBIDDEN_REQUEST，表示禁止请求。*/
    if(stat(m_real_file,&m_file_stat)<0)
    {
        return NO_RESOURCE;
    } if(!(m_file_stat.st_mode&S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }
//    判断文件是否为目录：通过 S_ISDIR(m_file_stat.st_mode) 来判断文件的类型是否为目录。
//    如果是目录，则返回 BAD_REQUEST，表示请求的文件格式不正确。
    if(S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }
//    打开文件
    int fd=open(m_real_file,O_RDONLY);
//   创建页空间 MAP_PRIVATE 1）PROT_READ，内存段可读； 内存段为调用进程所私有。对该内存段的修改不会反映到被映射的文件中。
    m_file_address= (char*)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);
    return FILE_REQUEST;
}
/*对内存映射区执行munmap操作*/
void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address=0;
    }
}
/*写HTTP响应*/
bool http_conn::write()
{
    int temp=0;
    int bytes_have_send=0;
    int bytes_to_send=m_write_idx;
    if(bytes_to_send==0)
    {
        /*此时可以修改 epoll 事件，监听套接字上的读事件（EPOLLIN），并重新初始化 HTTP 连接的状态，以便处理下一个请求。*/
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        init();
        return true;
    }
    while(1)
    {
        /*writev 函数用于将分散的数据写入文件描述符。在这里，writev 函数被用于将分散在多个缓冲区中的数据一次性写入到套接字 m_sockfd 中。
         * 在这里m_iv_count数量只能为2或1
         * 写到m_sockfd——Http响应*/
        temp=writev(m_sockfd,m_iv,m_iv_count);
        if(temp<=-1)
        {
            /*如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件。虽然在此期间，
             * 服务器无法立即接收到同一客户的下一个请求，但这可以保证连接的完整性
             *TCP 写缓冲没有空间意味着当前写操作无法立即完成，因为发送缓冲区已经满了。这通常发生在对端的接收速度比发送速度慢的情况下，
             * 或者在网络拥塞的情况下。当发生这种情况时，系统会阻塞或延迟写操作，直到发送缓冲区中有足够的空间来容纳要发送的数据。
               在这段代码中，如果 writev 函数返回 -1，并且 errno 的值是 EAGAIN，则说明写操作被阻塞，因为发送缓冲区已满。
               此时，服务器将修改 epoll 监听事件，等待下一次可写事件触发，以便继续发送数据。这样可以确保连接的完整性，并防止数据丢失。*/
        if(errno==EAGAIN)
            {
                modfd(m_epollfd,m_sockfd,EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send-=temp;
        bytes_have_send+=temp;
        if(bytes_to_send<=bytes_have_send)
        {
            /*发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接*/
            unmap();
            if(m_linger)
            {
                init();
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return true;
            }
            else
            {
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return false;
            }
        }
    }
}
/*往写缓冲中写入待发送的数据
 * 在 C/C++ 中，省略号 ... 表示可变数量的参数。在函数定义或声明中使用 ... 表示函数接受可变数量的参数。具体详情可以看obsidian内笔记*/
bool http_conn::add_response(const char*format,...)
{
//    写缓冲区中待发送的字节数>=/*写缓冲区的大小*/
    if(m_write_idx>=WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;
//    按照format的类型来读如数据
    va_start(arg_list,format);
    /*这段代码使用vsnprintf函数向m_write_buf中写入格式化的字符串。参数m_write_buf+m_write_idx指定了写入的起始位置，
      WRITE_BUFFER_SIZE-1-m_write_idx指定了最大可写入的字符数。format是格式化字符串，arg_list是参数列表。
      vsnprintf函数会根据format字符串中的格式说明符（比如%d、%s等）来格式化参数，并将结果写入到指定的缓冲区中。最终，
      vsnprintf返回实际写入缓冲区的字符数len。
     * */
    int len=vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);

    if(len>=(WRITE_BUFFER_SIZE-1-m_write_idx))
    {
        return false;
    }
    m_write_idx+=len;
    va_end(arg_list);
    return true;
}
bool http_conn::add_status_line(int status,const char*title) {
    return add_response("%s%d%s\r\n","HTTP/1.1",status,title);
}
bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n",content_len);
}
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n",(m_linger==true)?"keep-alive":"close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s","\r\n");
}
bool http_conn::add_content(const char*content) {
    return add_response("%s",content);
}
/*根据服务器处理HTTP请求的结果，决定返回给客户端的内容*/
bool http_conn::process_write(HTTP_CODE ret)
{
    switch(ret)
    {
//        未知错误
        case INTERNAL_ERROR:
        {
            add_status_line(500,error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form))
            {
                return false;
            }
            break;
        }

        case BAD_REQUEST:
        {
            add_status_line(400,error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form))
            {
                return false;
            }
            break;
        }
        case NO_RESOURCE:
        {
            add_status_line(404,error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form))
            {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403,error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form))
            {
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200,ok_200_title);
            if(m_file_stat.st_size!=0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base=m_write_buf;
                m_iv[0].iov_len=m_write_idx;
                m_iv[1].iov_base=m_file_address;
                m_iv[1].iov_len=m_file_stat.st_size;
                m_iv_count=2;
                return true;
            }
            else
            {
                const char*ok_string="<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string))
                {
                    return false;
                }
            }
        }
        default:
        {
            return false;
        }
    }
//    为什么要这样
    m_iv[0].iov_base=m_write_buf;
    m_iv[0].iov_len=m_write_idx;
    m_iv_count=1;
    return true;
} /*由线程池中的工作线程调用，这是处理HTTP请求的入口函数*/
void http_conn::process()
{
    HTTP_CODE read_ret=process_read();
    if(read_ret==NO_REQUEST)
    {
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return;
    }
    bool write_ret=process_write(read_ret);
    if(!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT);
}