/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh 
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
	    clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); 
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, NI_NUMERICHOST | NI_NUMERICSERV);
    /*getnameinfo 解析出客户端的主机和端口，0表示默认返回host中的域名，默认返回服务*/
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd); 
        Close(connfd); 
    }
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd) 
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))  //读取请求行到缓冲区buf
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);  //获取请求行的方法,统一资源标识符和HTTP版本

    /*tiny只支持GET方法，如果客户端请求其它方法则发送一个错误消息*/
    if (strcasecmp(method, "GET")) { 
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    }                                                    
    read_requesthdrs(&rio);                              

    /*uri为文件名与CGI程序的参数字符串*/
    is_static = parse_uri(uri, filename, cgiargs); 
    //stat获取filename指向的文件的元数据放入sbuf中，成功返回0，失败返回-1   
    if (stat(filename, &sbuf) < 0) {                     
        clienterror(fd, filename, "404", "Not found",
                "Tiny couldn't find this file");
        return;
    }                                                    

    if (is_static) {    /*Server static content*/
        //如果不是普通文件或者拥有者不能读这个文，则打印错误   
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { 
            clienterror(fd, filename, "403", "Forbidden",
                "Tiny couldn't read the file");
            return;
        }
	    serve_static(fd, filename, sbuf.st_size);        
    }
    else { /* Serve dynamic content */
        //如果文件不是普通文件或者不是可执行文件，则打印错误
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden",
                "Tiny couldn't run the CGI program");
            return;
        }
        serve_dynamic(fd, filename, cgiargs);
    }
}

/*
 * read_requesthdrs 读取并忽略请求报头
 * 终止请求报头的空文本行是由"\r\n"组成的
 */
void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) {  //当匹配到空行时终止循环
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);//读取到请求报头直接打印，不进行其他操作
    }
    return;
}

/**
 * parse_uri解析静态内容，获取Linux相对路径名filename和cgi文件参数cgiargs
 * tiny假设静态内容的主目录为它的当前目录，可执行文件的主目录是./cgi-bin
 * 任何包含字符串cgi-bin的URI都会被认为表示的是对静态内容的请求
 */
int parse_uri(char *uri, char *filename, char *cgiargs) 
{
    char *ptr;
    /*strstr找到第一次出现指定字符串的位置，未找到返回NULL*/
    if (!strstr(uri, "cgi-bin")) { //请求静态资源，将uri转换为Linux相对路径名
        strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);
        if (uri[strlen(uri)-1] == '/')
            strcat(filename, "home.html");//默认文件"home.html"加到filename后面
        return 1;
    }
    else {  /* Dynamic content */ 
	    ptr = index(uri, '?');//找到第一个字符'?'的位置

        if (ptr) { //动态文件的参数存在
            strcpy(cgiargs, ptr+1);//cgiargs为cgi文件保存参数
            *ptr = '\0';//将'?'替换为空字符'\0',方便解析出文件路径
        }
        else 
            strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);//将uri追加到filename后
        return 0;
    }
}

/**
 * serve_static 发送一个HTTP响应，其主体包含一个本地文件的内容
 * 通过检查文件名的后缀来判断文件类型，并且发送相应行和响应报头给客户端
 * 注意：用空行终止报头
 */
void serve_static(int fd, char *filename, int filesize)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

/*get_filetype等很多函数都是将局部指针变量传入作为参数,而不是使用静态或全局变量
    这样保证了函数的可重入性，当被多个线程调用时，不会引用任何共享数据*/

    /* Send response headers to client */
    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n", filesize);//响应主体的字节大小
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: %s\r\n\r\n", filetype);//响应主题的MIME类型
    Rio_writen(fd, buf, strlen(buf));

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);
    /*mmap映射大小为filesize文件描述符为srcfd的文件,到虚拟内存页。被映射的对象是私有的，写时复制的*/
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    Close(srcfd);//关闭文件描述符
    Rio_writen(fd, srcp, filesize);
/*munmap删除从srcp开始，大小为filesize的虚拟内存区域，接下来对这个区域的引用会造成段错误*/
    Munmap(srcp, filesize);
}
/** http://49.234.120.83:8863/home.html 
 * GET /movie.ogg HTTP/1.1
 * Host: 49.234.120.83:8863
*/

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
	    strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
	    strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
	    strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
	    strcpy(filetype, "image/jpeg");
    else
	    strcpy(filetype, "text/plain");
}  

/**
 * serve_dynamic 向客户端发送一个表明成功的响应行，同时包括带有信息的Server报头
 */
void serve_dynamic(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
  
    if (Fork() == 0) { /*子进程*/
        /*环境变量中添加"QUERY_STRING=cgiargs"的条目，第三个参数为1表示如果存在这个key，则更新value*/
        setenv("QUERY_STRING", cgiargs, 1);
        Dup2(fd, STDOUT_FILENO);//IO重定向,将描述符fd的条目替换标准输出(1)的条目
        /*运行CGI程序,该程序会利用getenv("QUERY_STRING")得到指向value的指针*/
        Execve(filename, emptylist, environ);
    }
    /*waitpid(-1, NULL, 0), 父进程等待并回收终止的子进程*/
    Wait(NULL); 
}
/* http://49.234.120.83:8863/cgi-bin/adder?100&20 */

/**
 * clienterror 发送一个HTTP响应到客户端，在响应行中包含相应的状态码和状态信息
 * 响应主体包含一个HTML文件，向浏览器用户解释这个错误
 */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
