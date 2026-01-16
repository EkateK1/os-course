#include <linux/types.h>

#define VTKM_DEV_NAME "vtkm"
#define TCP_IOC_MAGIC   't'
#define TCP_MAX_BATCH   128

#define TCP_IOC_COMMAND _IOWR(TCP_IOC_MAGIC, 1, struct tcp_req)

struct tcp_data {
    int sl;

    /* local_address / rem_address */
    __be32 src;
    __be32 dst;
    __u16 srcp;
    __u16 dstp;

    int state;

    /* queues */
    __u32 tx_queue; 
    int rx_queue;

    /* timer */
    int timer_active;
    unsigned long tm_when;

    /* retransmits */
    unsigned int retrnsmt;

    unsigned int uid;
    int timeout; 
    unsigned long inode;

    int refcount;
    void* sk;
    unsigned long rto;        
    unsigned long ato;          
    unsigned int qack_pingpong;
    unsigned int snd_cwnd;
    int snd_ssthresh_or_fqlen;
};

struct tcp_req {
    int pid;
    unsigned int offset;

    unsigned int got;
    unsigned int more;

    struct tcp_data rows[TCP_MAX_BATCH];
};
