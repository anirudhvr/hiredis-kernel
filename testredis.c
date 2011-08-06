/* Adapted from hiredis's test.c in hiredis by avr */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/file.h>
#include <asm/uaccess.h>

#include "redisclient.h"

#define SERVER_IP "172.16.174.1"
#define SERVER_PORT 6379

/* The following line is our testing "framework" :) */
#define test_cond(_c) if(_c) printk(KERN_INFO "PASSED\n"); else {printk(KERN_INFO "FAILED\n"); fails++;}

static int __init testredis_init(void)
{
        int fd;
        int fails = 0;
        redisReply *reply;

        printk(KERN_INFO "testredis_init() called\n");

        reply = redisConnect(&fd, SERVER_IP, SERVER_PORT);
        if (reply != NULL) {
                printk(KERN_INFO "Connection error: %s", reply->reply);
                return 1;
        }

        /* test 1 */
        printk(KERN_INFO "\n#1 Is able to deliver commands: ");
        reply = redisCommand(fd, "PING");
        test_cond(reply->type == REDIS_REPLY_STRING &&
                  strcasecmp(reply->reply, "pong") == 0)
            /* Switch to DB 9 for testing, now that we know we can chat. */
        reply = redisCommand(fd, "SELECT 9");
        freeReplyObject(reply);

        /* Make sure the DB is emtpy */
        reply = redisCommand(fd, "DBSIZE");
        if (reply->type != REDIS_REPLY_INTEGER || reply->integer != 0) {
                printk(KERN_INFO
                       "Sorry DB 9 is not empty, test can not continue\n");
                return 1;
        } else {
                printk(KERN_INFO "DB 9 is empty... test can continue\n");
        }
        freeReplyObject(reply);

        /* test 2 */
        printk(KERN_INFO "#2 Is a able to send commands verbatim: ");
        reply = redisCommand(fd, "SET foo bar");
        test_cond(reply->type == REDIS_REPLY_STRING &&
                  strcasecmp(reply->reply, "ok") == 0) freeReplyObject(reply);

        /* test 3 */
        printk(KERN_INFO "#3 %%s String interpolation works: ");
        reply = redisCommand(fd, "SET %s %s", "foo", "hello world");
        freeReplyObject(reply);
        reply = redisCommand(fd, "GET foo");
        test_cond(reply->type == REDIS_REPLY_STRING &&
                  strcmp(reply->reply, "hello world") == 0);
        freeReplyObject(reply);

        /* test 4 & 5 */
        printk(KERN_INFO "#4 %%b String interpolation works: ");
        reply = redisCommand(fd, "SET %b %b", "foo", 3, "hello\x00world", 11);
        freeReplyObject(reply);
        reply = redisCommand(fd, "GET foo");
        test_cond(reply->type == REDIS_REPLY_STRING &&
                  memcmp(reply->reply, "hello\x00world", 11) == 0)
            printk(KERN_INFO "#5 binary reply length is correct: ");
        test_cond(sdslen(reply->reply) == 11) freeReplyObject(reply);

        /* test 6 */
        printk(KERN_INFO "#6 can parse nil replies: ");
        reply = redisCommand(fd,"GET nokey");
        printk(KERN_INFO "Received %c %d\n", reply->type, reply->type);
        test_cond(reply->type == REDIS_REPLY_NIL) freeReplyObject(reply);

        /* test 7 */
        printk(KERN_INFO "#7 can parse integer replies: ");
        reply = redisCommand(fd, "INCR mycounter");
        test_cond(reply->type == REDIS_REPLY_INTEGER
                  && reply->integer == 1) freeReplyObject(reply);

        /* test 8 */
        printk(KERN_INFO "#8 can parse multi bulk replies: ");
        freeReplyObject(redisCommand(fd, "LPUSH mylist foo"));
        freeReplyObject(redisCommand(fd, "LPUSH mylist bar"));
        reply = redisCommand(fd, "LRANGE mylist 0 -1");
        test_cond(reply->type == REDIS_REPLY_ARRAY &&
                  reply->elements == 2 &&
                  !memcmp(reply->element[0]->reply, "bar", 3) &&
                  !memcmp(reply->element[1]->reply, "foo", 3))
            freeReplyObject(reply);

        /* Clean DB 9 */
        reply = redisCommand(fd, "FLUSHDB");
        freeReplyObject(reply);

        if (fails == 0) {
                printk(KERN_INFO "ALL TESTS PASSED\n");
        } else {
                printk(KERN_INFO "*** %d TESTS FAILED ***\n", fails);
        }

        return 0;
}

void __exit testredis_exit(void)
{
        printk(KERN_INFO "testredis_exit() called\n");
}

module_init(testredis_init);
module_exit(testredis_exit);

MODULE_AUTHOR("avr");
MODULE_DESCRIPTION("testredis");
MODULE_VERSION("0.01");
MODULE_LICENSE("GPL");
/* ex: set shiftwidth=4 expandtab: */
