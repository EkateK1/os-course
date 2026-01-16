#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "../common/vtkm.h"

static void print_row(const struct tcp_data *r)
{
    printf("%4d: %08X:%04X %08X:%04X %02X %08X:%08X %02X:%08lX "
			"%08X %5u %8d %lu %d %pK %lu %lu %u %u %d\n",
           r->sl,
           r->src, r->srcp,
           r->dst, r->dstp,
           r->state,
           r->tx_queue,
           r->rx_queue,
           r->timer_active,
           r->tm_when,
           r->retrnsmt,
           r->uid,
           r->timeout,
           r->inode,
           r->refcount,
           r->sk,
           r->rto,
           r->ato,
           r->qack_pingpong,
           r->snd_cwnd,
           r->snd_ssthresh_or_fqlen);
}

int main(int argc, char **argv){
    int pid;
    int fd;

    if (argc == 1){
        pid = 0;
    } else if (argc == 2){
        pid = atoi(argv[1]);
    } else {
        printf("Invalid args amount");
        return 1;
    }

    fd = open("/dev/vtkm", O_RDONLY);
    if (fd < 0) {
        perror("open(/dev/vtkm)");
        return 1;
    }

    struct tcp_req* req = calloc(1, sizeof(struct tcp_req));
    req->pid = pid;
    req->offset = 0;

    printf("  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ...\n");

     while (1) {
        if (ioctl(fd, TCP_IOC_COMMAND, req) != 0) {
            perror("ioctl(TCPD_IOC_DUMP)");
            close(fd);
            return 1;
        }

        for (unsigned int i = 0; i < req->got; i++) {
            print_row(&req->rows[i]);
        }

        if (!req->more)
            break;

        req->offset += req->got;
    }

    close(fd);
    return 0;
}