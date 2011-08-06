/*
   adapted from the hiredis client library by avr
 */

#include "redisclient.h"

static redisReply *redisReadReply(int fd);
static redisReply *createReplyObject(int type, sds reply);

/* We simply abort on out of memory */
static void redisOOM(void) {
    printk(KERN_ERR "Out of memory in redisclient.c");
}

/* Connect to a Redis instance. On success NULL is returned and *fd is set
 * to the socket file descriptor. On error a redisReply object is returned
 * with reply->type set to REDIS_REPLY_ERROR and reply->string containing
 * the error message. This replyObject must be freed with redisFreeReply(). */
/*
   redisReply *redisConnect(int *fd, const char *ip, int port) {
   char err[REDIS_ERR_LEN];

 *fd = anetTcpConnect(err,ip,port);
 if (*fd == ANET_ERR)
 return createReplyObject(REDIS_REPLY_ERROR,sdsnew(err));
 anetTcpNoDelay(NULL,*fd);
 return NULL;
 }
 */

redisReply* redisConnect(int *fd, const char *ip, int port)
{
    int rc;
    struct socket *sock;
    struct sockaddr_in sin;

    rc = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
    if (!rc) {  
        /* success */
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = in_aton(ip);
        sin.sin_port = htons(port);

        rc = sock->ops->connect(sock, (struct sockaddr*)&sin, 
                sizeof(sin), 0);
        if (!rc) 
            rc = kernel_tcpnodelay(sock); /* set TCP_NODELAY for socket */
        else
            return createReplyObject(REDIS_REPLY_ERROR, 
                    sdsnew("Cannot connect to the IP port combo"));

    } else {
        return createReplyObject(REDIS_REPLY_ERROR, 
                sdsnew("Cannot create socket!"));
    }

    *fd = sock_map_fd(sock);
    if (*fd < 0)
        return createReplyObject(REDIS_REPLY_ERROR, 
                sdsnew("Cannot do sock_map_fd!"));

    return NULL;
}

/* Create a reply object */
static redisReply *createReplyObject(int type, sds reply) {
    redisReply *r = kmalloc(sizeof(*r), GFP_KERNEL);

    if (!r) redisOOM();
    r->type = type;
    r->reply = reply;
    return r;
}

/* Free a reply object */
void freeReplyObject(redisReply *r) {
    size_t j;

    switch(r->type) {
        case REDIS_REPLY_INTEGER:
            break; /* Nothing to free */
        case REDIS_REPLY_ARRAY:
            for (j = 0; j < r->elements; j++)
                freeReplyObject(r->element[j]);
            kfree(r->element);
            break;
        default:
            sdsfree(r->reply);
            break;
    }
    kfree(r);
}

static redisReply *redisIOError(void) {
    return createReplyObject(REDIS_REPLY_ERROR,sdsnew("I/O error"));
}

/* modified by avr due to syscalls */
/* In a real high performance C client this should be bufferized */
static sds redisReadLine(int fd) {
    mm_segment_t old_fs;

    sds line = sdsempty();
    old_fs = get_fs();
    set_fs(KERNEL_DS);

    while(1) {
        char c;
        ssize_t ret;

        ret = sys_read(fd,&c,1);
        if (ret == -1) {
            sdsfree(line);
            set_fs(old_fs);
            return NULL;
        } else if ((ret == 0) || (c == '\n')) {
            break;
        } else {
            line = sdscatlen(line,&c,1);
        }
    }

    set_fs(old_fs);
    return sdstrim(line,"\r\n");
}

static redisReply *redisReadSingleLineReply(int fd, int type) {
    sds buf = redisReadLine(fd);

    if (buf == NULL) return redisIOError();
    return createReplyObject(type,buf);
}

static redisReply *redisReadIntegerReply(int fd) {
    sds buf = redisReadLine(fd);
    redisReply *r = kmalloc(sizeof(*r), GFP_KERNEL);

    if (r == NULL) redisOOM();
    if (buf == NULL) {
        kfree(r);
        return redisIOError();
    }
    r->type = REDIS_REPLY_INTEGER;
    r->integer = simple_strtoll(buf,NULL,10);
    sdsfree(buf);
    return r;
}

static redisReply *redisReadBulkReply(int fd) {
    sds replylen = redisReadLine(fd);
    sds buf;
    char crlf[2];
    int bulklen;

    if (replylen == NULL) return redisIOError();
    bulklen = (int)simple_strtol(replylen, (char **)NULL, 10);
    sdsfree(replylen);
    if (bulklen == -1)
        return createReplyObject(REDIS_REPLY_NIL,sdsempty());

    buf = sdsnewlen(NULL,bulklen);
    kernel_anetRead(fd,buf,bulklen);
    kernel_anetRead(fd,crlf,2);
    return createReplyObject(REDIS_REPLY_STRING,buf);
}

static redisReply *redisReadMultiBulkReply(int fd) {
    sds replylen = redisReadLine(fd);
    long elements, j;
    redisReply *r;

    if (replylen == NULL) return redisIOError();
    elements = simple_strtol(replylen,NULL,10);
    sdsfree(replylen);

    if (elements == -1)
        return createReplyObject(REDIS_REPLY_NIL,sdsempty());

    if ((r = kmalloc(sizeof(*r), GFP_KERNEL)) == NULL) redisOOM();
    r->type = REDIS_REPLY_ARRAY;
    r->elements = elements;
    if ((r->element = kmalloc(sizeof(*r)*elements, GFP_KERNEL)) == NULL) redisOOM();
    for (j = 0; j < elements; j++)
        r->element[j] = redisReadReply(fd);
    return r;
}

static redisReply *redisReadReply(int fd) {
    char type;

    if (kernel_anetRead(fd,&type,1) <= 0) return redisIOError();
    switch(type) {
        case '-':
            return redisReadSingleLineReply(fd,REDIS_REPLY_ERROR);
        case '+':
            return redisReadSingleLineReply(fd,REDIS_REPLY_STRING);
        case ':':
            return redisReadIntegerReply(fd);
        case '$':
            return redisReadBulkReply(fd);
        case '*':
            return redisReadMultiBulkReply(fd);
        default:
            printk(KERN_ERR "protocol error, got '%c' as reply type byte\n", type);
            return NULL;
    }
}

/* Helper function for redisCommand(). It's used to append the next argument
 * to the argument vector. */
static void addArgument(sds a, char ***argv, int *argc) {
    (*argc)++;
    if ((*argv = krealloc(*argv, sizeof(char*)*(*argc), GFP_KERNEL)) == NULL) 
        redisOOM();
    (*argv)[(*argc)-1] = a;
}

/* Execute a command. This function is printf alike:
 *
 * %s represents a C nul terminated string you want to interpolate
 * %b represents a binary safe string
 *
 * When using %b you need to provide both the pointer to the string
 * and the length in bytes. Examples:
 *
 * redisCommand("GET %s", mykey);
 * redisCommand("SET %s %b", mykey, somevalue, somevalue_len);
 *
 * RETURN VALUE:
 *
 * The returned value is a redisReply object that must be freed using the
 * redisFreeReply() function.
 *
 * given a redisReply "reply" you can test if there was an error in this way:
 *
 * if (reply->type == REDIS_REPLY_ERROR) {
 *     printf("Error in request: %s\n", reply->reply);
 * }
 *
 * The replied string itself is in reply->reply if the reply type is
 * a REDIS_REPLY_STRING. If the reply is a multi bulk reply then
 * reply->type is REDIS_REPLY_ARRAY and you can access all the elements
 * in this way:
 *
 * for (i = 0; i < reply->elements; i++)
 *     printf("%d: %s\n", i, reply->element[i]);
 *
 * Finally when type is REDIS_REPLY_INTEGER the long long integer is
 * stored at reply->integer.
 */
redisReply *redisCommand(int fd, const char *format, ...) {
    va_list ap;
    size_t size;
    const char *arg, *c = format;
    sds cmd = sdsempty();     /* whole command buffer */
    sds curr_arg = sdsempty(); /* current argument */
    char **argv = NULL;
    int argc = 0, j;

    /* Build the command string accordingly to protocol */
    va_start(ap,format);
    while(*c != '\0') {
        if (*c != '%' || c[1] == '\0') {
            if (*c == ' ') {
                if (sdslen(curr_arg) != 0) {
                    addArgument(curr_arg, &argv, &argc);
                    curr_arg = sdsempty();
                }
            } else {
                curr_arg = sdscatlen(curr_arg,c,1);
            }
        } else {
            switch(c[1]) {
                case 's':
                    arg = va_arg(ap,char*);
                    curr_arg = sdscat(curr_arg,arg);
                    break;
                case 'b':
                    arg = va_arg(ap,char*);
                    size = va_arg(ap,size_t);
                    curr_arg = sdscatlen(curr_arg,arg,size);
                    break;
                case '%':
                    cmd = sdscat(cmd,"%");
                    break;
            }
            c++;
        }
        c++;
    }
    va_end(ap);

    /* Add the last argument if needed */
    if (sdslen(curr_arg) != 0)
        addArgument(curr_arg, &argv, &argc);
    else
        sdsfree(curr_arg);

    /* Build the command at protocol level */
    cmd = sdscatprintf(cmd,"*%d\r\n",argc);
    for (j = 0; j < argc; j++) {
        cmd = sdscatprintf(cmd,"$%zu\r\n",sdslen(argv[j]));
        cmd = sdscatlen(cmd,argv[j],sdslen(argv[j]));
        cmd = sdscatlen(cmd,"\r\n",2);
        sdsfree(argv[j]);
    }
    kfree(argv);

    /* Send the command via socket */
    kernel_anetWrite(fd,cmd,sdslen(cmd));
    sdsfree(cmd);
    return redisReadReply(fd);
}


