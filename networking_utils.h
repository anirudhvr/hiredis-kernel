#ifndef __LKERNEL_NETWORKING_H
#define __LKERNEL_NETWORKING_H 

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <net/sock.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <net/protocol.h>
#include <net/udp.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/hardirq.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>
#include <linux/file.h>
#include <linux/fs.h>

int kernel_anetRead(int fd, char *buf, int count);
int kernel_anetWrite(int fd, char *buf, int count);
int kernel_setsockopt(struct socket *sock, int level, int optname,
        char __user *optval, int optlen);
int kernel_tcpnodelay(struct socket *sock);
/* The following are lifted from some linuxforums article on networking
  from within the kernel */
size_t SendBuffer(struct socket *sock, const char *Buffer, size_t
        Length);
size_t RecvBuffer(struct socket *sock, const char *Buffer, size_t
        Length);
struct socket* set_up_server_socket(int port_no);
struct socket* server_accept_connection(struct socket *sock);
struct socket* set_up_client_socket(unsigned int IP_addr, int port_no);

#endif 
