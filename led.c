#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/ctype.h>


// #define MAGIC_NO		100
// #define SET_SHUT_CMD		_IOW(MAGIC_NO, 0, char*)
// #define SET_SHUT_TIME		_IOW(MAGIC_NO, 1, int)

#define RED_PIN			16
#define GREEN_PIN		20
#define BLUE_PIN		21



#define DRIVER_AUTHOR "tar9397 <tar9397@gmail.com>"
#define DRIVER_DESC   "A sample loadable kernel module"

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC); 
MODULE_VERSION("0.1");
// MODULE_SUPPORTED_DEVICE("testdevice"); /* kieu device ma module ho tro */
typedef struct led_dev {
	u8 red_pin;
	u8 green_pin;
	u8 blue_pin;
	u8 value;
} led_dev_t;



struct _led_drv {
	dev_t dev_num;
	struct class *dev_class;
	struct device *dev;
	led_dev_t *hw;
	struct cdev *vcdev;
} led_drv;

// ---------------------------------------------------------------------------- 
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
static int vchar_hw_init(led_dev_t * hw) {
  hw->red_pin = RED_PIN;
  hw->green_pin = GREEN_PIN;
  hw->blue_pin = BLUE_PIN;

  gpio_request(hw->red_pin, "RED_PIN");
  gpio_request(hw->green_pin, "GREEN_PIN");
  gpio_request(hw->blue_pin, "BLUE_PIN");

  gpio_direction_output(hw->blue_pin, 1);
  gpio_direction_output(hw->green_pin, 1);
  gpio_direction_output(hw->red_pin, 1);

  return 0;
}

static void vchar_hw_exit(led_dev_t * hw) {
  gpio_free(hw->red_pin);
  gpio_free(hw->green_pin);
  gpio_free(hw->blue_pin);
}

static int vchar_hw_wrire_data(led_dev_t * hw, int start_reg , int len, char* kbuf) {
	int rgb_value = -1;

	if (!kstrtouint(kbuf, 10, &rgb_value)) {
		pr_debug("value write is %d\n",rgb_value);
		if (rgb_value < 8) {
		  gpio_set_value(hw->blue_pin, !(rgb_value & 0b00000001));
		  gpio_set_value(hw->green_pin, !(rgb_value & 0b00000010));
		  gpio_set_value(hw->red_pin, !(rgb_value & 0b00000100));
		  hw->value = rgb_value; 
		}
	}
  //ignore valid char
  return len; 
 }

static int vchar_hw_read_data(led_dev_t * hw, int start_reg , int len, char* kbuf) {
  u8 rgb_value = 0;
  
  // if (start_reg != 0)
  // 	return 0;

  hw->value = 0;

  // invert logic IO
  rgb_value = gpio_get_value(hw->blue_pin) != 0 ? 0 : 1;
  hw->value += rgb_value;
  rgb_value = gpio_get_value(hw->green_pin) != 0 ? 0 : 1;
  hw->value += (rgb_value<<1);
  rgb_value = gpio_get_value(hw->red_pin) != 0 ? 0 : 1;
  hw->value += (rgb_value<<2);

  // *kbuf = hw->value + 0x30;
  // pr_info("value read is %d\n",hw->value);

  // return len; 
  return sprintf(kbuf,"%d\n",hw->value); 
}



// ----------------------------------------------------------------------------

// file operation
static int dev_open(struct inode *inodep, struct file *filep) {
	pr_info("open is called\n");
	return 0;
}

static int dev_close(struct inode *inodep, struct file *filep) {
	pr_info("close is called\n");
	return 0;
}

static ssize_t dev_read(struct file *filep, char __user *user_buf, size_t len, loff_t *offset) {
	char *kernel_buf = NULL;
	int num_bytes = 0;
	pr_info("Handle read event start from %lld, %zu bytes\n", *offset, len);

	kernel_buf = kzalloc(len, GFP_KERNEL);
	if (NULL==kernel_buf) 
		return -ENOMEM;

	// num_bytes = vchar_hw_read_data(led_drv.hw, *offset, len, kernel_buf);
	// device with ony 1 byte
	if (0 != (*offset)) {
		num_bytes = 0;
	} else
		num_bytes = vchar_hw_read_data(led_drv.hw, *offset, len, kernel_buf);

	pr_info("Read %d bytes from HW\n", num_bytes);
	if (num_bytes < 0)
		return -EFAULT;

	if (copy_to_user(user_buf, kernel_buf, num_bytes)) 
		return -EFAULT;

	*offset += num_bytes;
	return num_bytes;
}

static ssize_t dev_write(struct file *filep, const char __user *user_buf, size_t len, loff_t *offset) {
	char *kernel_buf = NULL;
	int num_bytes = 0;
	pr_info("Handle write event start from %lld, %zu bytes\n", *offset, len);

	kernel_buf = kzalloc(len, GFP_KERNEL);
	if (NULL==kernel_buf) 
		return -ENOMEM;
	if (copy_from_user(kernel_buf, user_buf, len)) 
		return -EFAULT;

	num_bytes = vchar_hw_wrire_data(led_drv.hw, *offset, len, kernel_buf);
	if (num_bytes < 0)
		return -EFAULT;
	pr_info("Write %d bytes to HW\n", num_bytes);

	*offset += num_bytes;
	// return num_bytes;

	return len;
}


static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = dev_open,
	.release = dev_close,
	.read = dev_read,
	.write = dev_write,
};




// Init Driver

static int __init led_rgb_drv_init(void)
{
	int ret;

	// Register device number
	led_drv.dev_num = 0;
	ret = alloc_chrdev_region(&led_drv.dev_num, 0, 1, "led_device");
	if (ret) {
		pr_info("Can not register major number\n");
		goto fail_reg;
	}
	pr_info("Register successfully: major,minor is %d,%d\n", MAJOR(led_drv.dev_num),MINOR(led_drv.dev_num));

	// Create devie file
	led_drv.dev_class = class_create(THIS_MODULE, "class_led_dev");
	if (IS_ERR(led_drv.dev_class)) {
		pr_info("create class failed\n");
		goto fail_create_class;
	}
	pr_info("create successfully class\n");
	led_drv.dev = device_create(led_drv.dev_class, NULL, led_drv.dev_num, NULL, "led_dev");
	if (IS_ERR(led_drv.dev)) {
		pr_info("Create device failed\n");
		goto dev_fail;
	}
	pr_info("create device success\n");


	led_drv.hw = kzalloc(sizeof(led_dev_t), GFP_KERNEL);
	if (!led_drv.hw) {
		pr_info("Failed to allocate data stucture of the driver\n");
		ret = -ENOMEM;
		goto failed_allocated_structure;
	}
	ret = vchar_hw_init(led_drv.hw);
	if (ret < 0) {
		pr_info("Failed to init device\n");
		goto failed_init_hw;
	}


	led_drv.vcdev = cdev_alloc();
	if (led_drv.vcdev == NULL) {
		pr_info("Failed to allocate cdev stucture \n");
		ret = -ENOMEM;
		goto failed_allocated_cdev;
	}
	cdev_init(led_drv.vcdev, &fops);
	ret = cdev_add(led_drv.vcdev, led_drv.dev_num, 1);
	if (ret < 0) {
		pr_info("failed to add a char device to the system\n");
		goto failed_allocated_cdev;
	}

	pr_info("Init driver successfully\n");
	return 0;

failed_allocated_cdev:
	vchar_hw_exit(led_drv.hw);
failed_init_hw:
	kfree(led_drv.hw);
failed_allocated_structure:
	device_destroy(led_drv.dev_class, led_drv.dev_num);
dev_fail:
	// cdev_del(&my_cdev);
	class_destroy(led_drv.dev_class);
fail_create_class:
	unregister_chrdev_region(led_drv.dev_num, 1);
fail_reg:
	return -ENODEV;

}

static void __exit led_rgb_drv_exit(void)
{	
	cdev_del(led_drv.vcdev);
	vchar_hw_exit(led_drv.hw);
	kfree(led_drv.hw);
	// cdev_del(&my_cdev);
	device_destroy(led_drv.dev_class , led_drv.dev_num);
	class_destroy(led_drv.dev_class);
	unregister_chrdev_region(led_drv.dev_num, 1);
	pr_info("led_drv destroy\n");
}

module_init(led_rgb_drv_init);
module_exit(led_rgb_drv_exit);
