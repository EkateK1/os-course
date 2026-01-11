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

static dev_t vtkm_dev;
static struct cdev vtkm_cdev;
static struct class *vtkm_class;


static const struct file_operations vtkm_fops = {
    .owner = THIS_MODULE
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
    vtkm_class = NULL;
  err_chrdev:
    unregister_chrdev_region(vtkm_dev, 1);
    return ret;
  err_cdev:
    cdev_del(&vtkm_cdev);
    return ret;
}

static void __exit vtkm_exit(void) {
  LOG("VTKM left the kernel\n");
}

module_init(vtkm_init);
module_exit(vtkm_exit);
