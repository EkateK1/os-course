#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/cdev.h>
#include <linux/err.h>

#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/inet_sock.h>
#include <net/tcp.h>
#include <net/inet_hashtables.h>

#include "../common/vtkm.h"

#define MODULE_NAME "vtkm"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("secs-dev");
MODULE_DESCRIPTION("A simple kernel module");

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)

dev_t vtkm_dev;
struct cdev vtkm_cdev;
struct class* vtkm_class;

static struct net* get_net_by_pid(int pid)
{
    struct net* net;

    if (pid == 0) {
        net = get_net(current->nsproxy->net_ns);
        return net;
    }

    net = get_net_ns_by_pid(pid);
    if (IS_ERR(net))
        return NULL;

    return net;
}

static void fill_stats(struct sock* sk, struct tcp_data* data, int sl){

  int timer_active;
	unsigned long timer_expires;
	const struct tcp_sock *tp = tcp_sk(sk);
	const struct inet_connection_sock *icsk = inet_csk(sk);
	const struct inet_sock *inet = inet_sk(sk);
	const struct fastopen_queue *fastopenq = &icsk->icsk_accept_queue.fastopenq;

	__be32 dest = inet->inet_daddr;
	__be32 src = inet->inet_rcv_saddr;
	__u16 destp = ntohs(inet->inet_dport);
	__u16 srcp = ntohs(inet->inet_sport);

  data->sl = sl;
  data->src = src;
  data->dst = dest;
  data->srcp = srcp;
  data->dstp = destp;

	int rx_queue;
	int state;

	if (icsk->icsk_pending == ICSK_TIME_RETRANS ||
	    icsk->icsk_pending == ICSK_TIME_REO_TIMEOUT ||
	    icsk->icsk_pending == ICSK_TIME_LOSS_PROBE) {
		timer_active	= 1;
		timer_expires	= icsk->icsk_timeout;
	} else if (icsk->icsk_pending == ICSK_TIME_PROBE0) {
		timer_active	= 4;
		timer_expires	= icsk->icsk_timeout;
	} else if (timer_pending(&sk->sk_timer)) {
		timer_active	= 2;
		timer_expires	= sk->sk_timer.expires;
	} else {
		timer_active	= 0;
		timer_expires = jiffies;
	}

	state = inet_sk_state_load(sk);
  data->state = state;

	if (state == TCP_LISTEN)
		rx_queue = READ_ONCE(sk->sk_ack_backlog);
	else
		rx_queue = max_t(int, READ_ONCE(tp->rcv_nxt) - READ_ONCE(tp->copied_seq), 0);

  data->tx_queue = READ_ONCE(tp->write_seq) - tp->snd_una;
  data->rx_queue = rx_queue;

  data->timer_active = timer_active;
  data->tm_when = jiffies_delta_to_clock_t(timer_expires - jiffies);
  data->retrnsmt = icsk->icsk_retransmits;
  data->uid = from_kuid_munged(&init_user_ns, sock_i_uid(sk));
  data->timeout = icsk->icsk_probes_out;
  data->inode = sock_i_ino(sk);

  data->refcount = refcount_read(&sk->sk_refcnt);
  data->sk = sk;
  data->rto = jiffies_to_clock_t(icsk->icsk_rto);
  data->ato = jiffies_to_clock_t(icsk->icsk_ack.ato);
  data->qack_pingpong = (icsk->icsk_ack.quick << 1) | inet_csk_in_pingpong_mode(sk);
  data->snd_cwnd = tcp_snd_cwnd(tp);
  data->snd_ssthresh_or_fqlen = (state == TCP_LISTEN ?
		    fastopenq->max_qlen :
		    (tcp_in_initial_slowstart(tp) ? -1 : tp->snd_ssthresh));
}

static void fill_stats_timewait(struct inet_timewait_sock *tw, struct tcp_data* data, int sl){
  long delta = tw->tw_timer.expires - jiffies;
	__be32 dest, src;
	__u16 destp, srcp;

	dest  = tw->tw_daddr;
	src   = tw->tw_rcv_saddr;
	destp = ntohs(tw->tw_dport);
	srcp  = ntohs(tw->tw_sport);

  data->sl = sl;
  data->src = src;
  data->dst = dest;
  data->srcp = srcp;
  data->dstp = destp;

  data->state = tw->tw_substate;
  data->tx_queue = 0;
  data->rx_queue = 0;
  data->timer_active = 3;
  data->tm_when =  jiffies_delta_to_clock_t(delta);
  data->retrnsmt = 0;
  data->uid = 0;
  data->timeout = 0;
  data->inode = 0;
  data->refcount = refcount_read(&tw->tw_refcnt);
  data->sk = tw;

  data->rto = 0;
  data->ato = 0;
  data->qack_pingpong = 0;
  data->snd_cwnd = 0;
  data->snd_ssthresh_or_fqlen = 0;
}

static void fill_stats_new_recv(struct request_sock *req, struct tcp_data* data, int sl){
  const struct inet_request_sock *ireq = inet_rsk(req);
	long delta = req->rsk_timer.expires - jiffies;

  data->sl = sl;
  data->src = ireq->ir_loc_addr;
  data->dst = ireq->ir_rmt_addr;
  data->srcp = ireq->ir_num;
  data->dstp = ntohs(ireq->ir_rmt_port);

  data->state = TCP_SYN_RECV;
  data->tx_queue = 0;
  data->rx_queue = 0;
  data->timer_active = 1;
  data->tm_when =  jiffies_delta_to_clock_t(delta);
  data->retrnsmt = req->num_timeout;
  data->uid = from_kuid_munged(&init_user_ns, sock_i_uid(req->rsk_listener));
  data->timeout = 0;
  data->inode = 0;
  data->refcount = 0;
  data->sk = req;

  data->rto = 0;
  data->ato = 0;
  data->qack_pingpong = 0;
  data->snd_cwnd = 0;
  data->snd_ssthresh_or_fqlen = 0;
}

static void count (struct net* net, struct tcp_req* req){

  struct inet_hashinfo* hinfo = net->ipv4.tcp_death_row.hashinfo;
  unsigned int idx = 0;
  int st;

  for (unsigned int i = 0; i <= hinfo->lhash2_mask; i++) {

    struct inet_listen_hashbucket *ilb2;
    struct sock* sk;
    struct hlist_nulls_node *node;
    ilb2 = &hinfo->lhash2[i];
    spin_lock_bh(&ilb2->lock);

    sk_nulls_for_each(sk, node, &ilb2->nulls_head) { 
      if (!net_eq(sock_net(sk), net)) continue; 
      if (sk->sk_family != AF_INET) continue; 
      if (idx++ < req->offset) continue;

      st = inet_sk_state_load(sk);
      if (st != TCP_LISTEN) continue; 

      if (req->got >= MAX_ROWS) { 
        req->more = 1; 
        break; 
      }
      fill_stats(sk, &req->rows[req->got], (int)(req->offset + req->got)); 
      req->got++; 
    } 
    spin_unlock_bh(&ilb2->lock);
  }

  for (unsigned int i = 0; i <= hinfo->ehash_mask; i++) {
		struct sock *sk;
		struct hlist_nulls_node *node;
		spinlock_t *lock = inet_ehash_lockp(hinfo, i);

		cond_resched();

		spin_lock_bh(lock);
		sk_nulls_for_each(sk, node, &hinfo->ehash[i].chain) {
			if (!net_eq(sock_net(sk), net)) continue; 
      if (sk->sk_family != AF_INET) continue; 
      if (idx++ < req->offset) continue; 
      if (req->got >= MAX_ROWS) { 
        req->more = 1; 
        break; 
      }

      st = inet_sk_state_load(sk);
      if (st == TCP_NEW_SYN_RECV) {
        fill_stats_new_recv((struct request_sock *)sk, &req->rows[req->got], (int)(req->offset + req->got));
        continue;
      }

      if (st == TCP_TIME_WAIT) {
        struct inet_timewait_sock* tw = inet_twsk(sk);
        fill_stats_timewait(tw, &req->rows[req->got], (int)(req->offset + req->got));
        req->got++;
        continue;
      }

      fill_stats(sk, &req->rows[req->got], (int)(req->offset + req->got)); 
      req->got++; 
		}
		spin_unlock_bh(lock);
	}
}

static long vtkm_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
  struct tcp_req req;
  struct net* net;

  if (copy_from_user(&req, (void __user*)arg, sizeof(req)))
    return -1;

  net = get_net_by_pid(req.pid);
  if (!net)
    return -1;

  rcu_read_lock();
  count(net, &req);
  rcu_read_unlock();

  put_net(net);

  if (copy_to_user((void __user*)arg, &req, sizeof(req)))
    return -1;

  return 0;
}

const struct file_operations vtkm_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = vtkm_ioctl
  };

static int __init vtkm_init(void) {
  LOG("VTKM joined the kernel\n");

  int ret;
  ret = alloc_chrdev_region(&vtkm_dev, 0, 1, VTKM_DEV_NAME);
  if (ret) {
    LOG("alloc_chrdev_region failed: %d\n", ret);
    return ret;
  }

  cdev_init(&vtkm_cdev, &vtkm_fops);
  ret = cdev_add(&vtkm_cdev, vtkm_dev, 1);
  if (ret)
    goto err_chrdev;
  
  vtkm_class = class_create(VTKM_DEV_NAME);
    if (IS_ERR(vtkm_class)) {
        ret = PTR_ERR(vtkm_class);
        goto err_cdev;
    }
  
  if (IS_ERR(device_create(vtkm_class, NULL, vtkm_dev, NULL, VTKM_DEV_NAME))) {
    ret = -EINVAL;
    LOG("device_create failed\n");
    goto err_class;
  }

  return 0;

  err_class:
    class_destroy(vtkm_class);
  err_cdev:
    cdev_del(&vtkm_cdev);
  err_chrdev:
    unregister_chrdev_region(vtkm_dev, 1);
    return ret;
}

static void __exit vtkm_exit(void) {
  device_destroy(vtkm_class, vtkm_dev);
  class_destroy(vtkm_class);
  cdev_del(&vtkm_cdev);
  unregister_chrdev_region(vtkm_dev, 1);

  LOG("VTKM left the kernel\n");
}

module_init(vtkm_init);
module_exit(vtkm_exit);
