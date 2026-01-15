#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/cdev.h>
#include <linux/err.h>

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

static int count (struct net* net, struct tcp_req* req){
  struct inet_hashinfo info = net->ipv4.tcp_death_row.hashinfo;
  
}

static long vtkm_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
  struct tcp_req req;
  struct net* net;
  int res;

  if (copy_from_user(&req, (void __user*)arg, sizeof(req)))
    return -EFAULT;

  net = get_net_by_pid(req.pid);
  if (!net)
    return -EINVAL;

  rcu_read_lock();
  res = count(net, &req);
  rcu_read_unlock();

  put_net(net);

  if (res)
    return res;

  if (copy_to_user((void __user*)arg, &req, sizeof(req)))
    return -EFAULT;

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
