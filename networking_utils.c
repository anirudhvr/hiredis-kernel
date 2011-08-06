#include "networking_utils.h"


/* modified from hiredis by avr */
/* Like read(2) but make sure 'count' is read before to return
 * (unless error or EOF condition is encountered) */
int kernel_anetRead(int fd, char *buf, int count)
{
    mm_segment_t oldfs;
    int nread, totlen = 0;

    oldfs = get_fs();
    set_fs(KERNEL_DS);

    while(totlen != count) {
        nread = sys_read(fd,buf,count-totlen);
        if (nread == 0)
            break;
        if (nread == -1) {
            totlen = -1;
            break;
        }
        totlen += nread;
        buf += nread;
    }

    set_fs(oldfs);

    return totlen;
}

/* modified from hiredis by avr */
/* Like write(2) but make sure 'count' is read before to return
 * (unless error is encountered) */
int kernel_anetWrite(int fd, char *buf, int count)
{
    mm_segment_t oldfs;
    int nwritten, totlen = 0;

    oldfs = get_fs();
    set_fs(KERNEL_DS);

    while(totlen != count) {
        nwritten = sys_write(fd,buf,count-totlen);
        if (nwritten == 0) 
            break;
        if (nwritten == -1) {
            totlen = -1;
            break;
        }
        totlen += nwritten;
        buf += nwritten;
    }

    set_fs(oldfs);

    return totlen;
}

/*
   Quicker anetTcpNoDelay (from hiredis) without calling 
   sys_setsockopt directly (which requires an fd instead of struct socket)
   Code copied from sys_setsockopt 
*/
int kernel_setsockopt(struct socket *sock, int level, int optname,
        char __user *optval, int optlen)
{
    mm_segment_t oldfs;
    int err = 0;

    oldfs = get_fs();
    set_fs(KERNEL_DS);

    if (sock != NULL)  {
        /* 
           skip security checking code from sys_setsockopt 
         */
        if (level == SOL_SOCKET) 
            err = sock_setsockopt(sock, level, optname, 
                    optval, optlen);
        else
            err = sock->ops->setsockopt(sock, level, optname, 
                    optval, optlen);
    }
    set_fs(oldfs);
    return err;
}

int kernel_tcpnodelay(struct socket *sock)
{
    int yes = 1;
    return kernel_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, 
            (char*)&yes, sizeof(yes));
}

/*
   Sendbuffer sends "Length" bytes from "Buffer" through the socket "sock".
 */
size_t SendBuffer(struct socket *sock, const char *Buffer, size_t Length)
{
    struct msghdr msg;
    mm_segment_t oldfs; // mm_segment_t is just a long
    struct iovec iov; // structure containing a base addr. and length
    int len2;

    //printk("Entering SendBuffer\n");

    msg.msg_name = 0;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1; //point to be noted
    msg.msg_control = NULL;
    msg.msg_controllen = 0;

    msg.msg_flags = MSG_NOSIGNAL;//0/*MSG_DONTWAIT*/;

    iov.iov_base = (char*) Buffer; // as we know that iovec is
    iov.iov_len = (__kernel_size_t) Length; // nothing but a base addr and length

    // #define get_fs() (current_thread_info()->addr_limit)
    // similar for set_fs;
    /*
       Therefore this line sets the "fs" to KERNEL_DS and saves its old value
     */
    oldfs = get_fs(); 
    set_fs(KERNEL_DS);

    /* Actual Sending of the Message */
    len2 = sock_sendmsg(sock,&msg,(size_t)(Length));

    /* retrieve the old value of fs (whatever it is)*/
    set_fs(oldfs);


    return len2;
}

/*
   Recieves data from the socket "sock" and puts it in the 'Buffer'.
   Returns the length of data recieved

   The Calling function must do a:
   Buffer = (char*) get_free_page(GFP_KERNEL);
   or a kmalloc to allocate kernel's memory
   (or it can use the kernel's stack space [very small] )

 */
size_t RecvBuffer(struct socket *sock, const char *Buffer, size_t Length)
{
    struct msghdr msg;
    struct iovec iov;

    int len;
    mm_segment_t oldfs;

    /* Set the msghdr structure*/
    msg.msg_name = 0;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    /* Set the iovec structure*/
    iov.iov_base = (void *) &Buffer[0];
    iov.iov_len = (size_t)Length;

    /* Recieve the message */
    oldfs = get_fs(); set_fs(KERNEL_DS);
    len = sock_recvmsg(sock,&msg,Length,0/*MSG_DONTWAIT*/); // let it wait if there is no message
    set_fs(oldfs);

    // if ((len!=-EAGAIN)&&(len!=0))
    // printk("RecvBuffer Recieved %i bytes \n",len);

    return len;
}

/*
   Sets up a server-side socket

   1. Create a new socket
   2. Bind the address to the socket
   3. Start listening on the socket
 */

struct socket* set_up_server_socket(int port_no) {
    struct socket *sock;
    struct sockaddr_in sin;

    int error;

    /* First create a socket */
    error = sock_create(PF_INET,SOCK_STREAM,IPPROTO_TCP,&sock) ;
    if (error<0)
        printk("Error during creation of socket; terminating\n");

    /* Now bind the socket */
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port_no);

    error = sock->ops->bind(sock,(struct sockaddr*)&sin,sizeof(sin));
    if (error<0)
    {
        printk("Error binding socket \n");
        return 0;
    }

    /* Now, start listening on the socket */
    error=sock->ops->listen(sock,32);
    if (error!=0)
        printk("Error listening on socket \n");

    /* Now start accepting */
    // Accepting is performed by the function server_accept_connection

    return sock;
}

/*
   Accepts a new connection (server calls this function)

   1. Create a new socket
   2. Call socket->ops->accept
   3. return the newly created socket
 */

struct socket* server_accept_connection(struct socket *sock) {
    struct socket * newsock;
    int error;

    /* Before accept: Clone the socket */

    error = sock_create(PF_INET,SOCK_STREAM,IPPROTO_TCP,&newsock);
    if (error<0)
        printk("Error during creation of the other socket; terminating\n");

    newsock->type = sock->type;
    newsock->ops=sock->ops;

    /* Do the actual accept */

    error = newsock->ops->accept(sock,newsock,0);


    if (error<0) {
        printk("Error accepting socket\n") ;
        return 0;
    }
    return newsock;
}

struct socket* set_up_client_socket(unsigned int IP_addr, int port_no)
{
    struct socket * clientsock;
    struct sockaddr_in sin;
    int error, i;

    /* First create a socket */
    error = sock_create(PF_INET,SOCK_STREAM,IPPROTO_TCP,&clientsock);
    if (error<0) {
        printk("Error during creation of socket; terminating\n");
        return 0;
    }

    /* Now bind and connect the socket */
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(IP_addr);
    sin.sin_port = htons(port_no);

    for(i=0;i<10;i++) {
        error = clientsock->ops->connect(clientsock,(struct sockaddr*)&sin,sizeof(sin),0);
        if (error<0) {
            printk("Error connecting client socket to server: %i, retrying .. %d \n",error, i);
            if(i==10-1) {
                printk("Giving Up!\n"); return 0;
            }
        }
        else break; //connected
    }

    return clientsock;
}


