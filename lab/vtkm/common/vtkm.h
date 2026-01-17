#include <linux/types.h>

#define VTKM_DEV_NAME "vtkm"
#define IOC_NUM 't'
#define MAX_ROWS 128

#define TCP_IOC_COMMAND _IOWR(IOC_NUM, 1, struct tcp_req)

struct tcp_data {
    int sl; // line number

    __be32 src; // local addres
    __be32 dst; // remote addres
    __u16 srcp; // local port
    __u16 dstp; // remote port

    int state; // tcp state

    __u32 tx_queue; // размер передающией очереди 
    int rx_queue; // размер приемной очереди

    int timer_active; // идентификатор активного таймера
    unsigned long tm_when; // через сколько сработает активный таймер

    unsigned int retrnsmt; // повторные отправки данных

    unsigned int uid;
    int timeout;
    unsigned long inode; 

    int refcount; // счетчик ссылок на сокет
    void* sk; // адрес сокета
    unsigned long rto; // таймаут ретрансмитов  
    unsigned long ato; // задержка аск     
    unsigned int qack_pingpong; // сколько осталось быстрых аск + режим пинпонга(?)
    unsigned int snd_cwnd; // максимальное количество неподтвержденных пакетов
    int snd_ssthresh_or_fqlen; // от состояния
};

struct tcp_req {
    int pid;
    unsigned int offset;

    unsigned int got;
    unsigned int more;
    struct tcp_data rows[MAX_ROWS];
};
