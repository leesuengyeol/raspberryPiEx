//================================
// Character device driver example 
// GPIO driver
//================================
#include <linux/fs.h>		//open(),read(),write(),close() Ŀ���Լ�
#include <linux/cdev.h>		//register_chrdev_region(), cdev_init()
				//cdev_add(),cdev_del()
#include <linux/module.h>
//#include <linux/io.h>		//ioremap(), iounmap()
#include <linux/uaccess.h>	//copy_from_user(), copy_to_user()
#include <linux/gpio.h>		//request_gpio(), gpio_set_value()
				//gpio_get_value(), gpio_free()
#include <linux/interrupt.h>	//gpio_to_irq(), request_irq(), free_irq()
#include <linux/timer.h>	//init_timer(), add_timer(), del_timer()
#include <linux/signal.h>	//signal�� ���
#include <linux/sched/signal.h>	//siginfo ����ü�� ����ϱ� ����
#include <linux/hrtimer.h>
#include <linux/ktime.h>


#define SEG_MAJOR	200
#define SEG_MINOR	0
#define DEVICE_NAME	"segtimer"
#define BLK_SIZE	100
//#define DEBUG

struct cdev seg_cdev;
struct class *class;
struct device *dev;

static char msg[BLK_SIZE] = { 0 };
static struct task_struct *task; 	//�½�ũ�� ���� ����ü
pid_t pid;
char pid_valid;
int iSEC;
int iMSEC;

static int seg_open(struct inode *, struct file *);
static int seg_close(struct inode *, struct file *);
static ssize_t seg_read(struct file *, char *buff, size_t, loff_t *);
static ssize_t seg_write(struct file *, const char *, size_t, loff_t *);

static struct file_operations seg_fops = {
	.owner = THIS_MODULE,
	.read = seg_read,
	.write = seg_write,
	.open = seg_open,
	.release = seg_close,
};

#define MS_TO_NS(x) (x * 1E6L)

static struct hrtimer hr_timer;

enum hrtimer_restart my_hrtimer_callback(struct hrtimer *timer)
{
	ktime_t currtime, interval;
	unsigned long delay_in_ms = 10L;	//100ms

	//10���� ����
	if (iSEC == 1)
	{
		pr_info("my_hrtimer_callback is done (%ld).\n", jiffies);
		return HRTIMER_NORESTART;
	}
	else
	{
		if (iMSEC == 99)
		{
			iSEC++;
			iMSEC = 0;
		}
		else
			iMSEC++;

		pr_info("Time:%d.%d\n", iSEC,iMSEC);
		
		currtime = ktime_get();
		interval = ktime_set(0, MS_TO_NS(delay_in_ms));
		hrtimer_forward(timer, currtime, interval);
		return HRTIMER_RESTART;
	}
}

static int seg_open(struct inode *inod, struct file *fil)
{
	//����� ��� ī��Ʈ�� ���� ��Ų��.
	try_module_get(THIS_MODULE);
	printk(KERN_INFO "7-Segment Device opened\n");
	return 0;
}


static int seg_close(struct inode *inod, struct file *fil)
{
	//����� ��� ī��Ʈ�� ���� ��Ų��.
	module_put(THIS_MODULE);
	printk(KERN_INFO "7-Segment Device closed\n");
	return 0;
}

static ssize_t seg_read(struct file *fil, char *buff, size_t len, loff_t *off)
{
	int count;

	//Ŀ�ο����� �ִ� msg���ڿ��� ���������� buff�ּҷ� ���� 
	count = copy_to_user(buff, msg, strlen(msg) + 1);

	printk(KERN_INFO "7-segment Device read:%s\n", msg);

	return count;
}


static ssize_t seg_write(struct file *fil, const char *buff, size_t len, loff_t *off)
{
	int count;
	char *endptr;

	memset(msg, 0, BLK_SIZE);

	count = copy_from_user(msg, buff, len);

	//pidstr���ڿ��� ���ڷ� ��ȯ
	pid = simple_strtol(msg, &endptr, 10);
	printk(KERN_INFO "pid=%d\n", pid);

	if (endptr != NULL)
	{
		// pid���� ���� task_struct����ü�� �ּҰ��� Ȯ�� 
		task = pid_task(find_vpid(pid), PIDTYPE_PID);
		if (task == NULL)
		{
			printk(KERN_INFO "Error:Can't find PID from user APP\n");
			return 0;
		}
	}

	printk(KERN_INFO "7-segment Device write:%s\n", msg);
	return count;
}

static int __init initModule(void)
{
	dev_t devno;
	int err;
	int count;
	ktime_t ktime;
	unsigned long delay_in_ms = 10L;  //10ms

	printk("Called initModule()\n");

	// 1. ���ڵ���̽� ����̹��� ����Ѵ�.
	devno = MKDEV(SEG_MAJOR, SEG_MINOR);
	register_chrdev_region(devno, 1, DEVICE_NAME);

	// 2. ���� ����̽��� ���� ����ü�� �ʱ�ȭ �Ѵ�.
	cdev_init(&seg_cdev, &seg_fops);
	seg_cdev.owner = THIS_MODULE;
	count = 1;

	// 3. ���ڵ���̽��� �߰�
	err = cdev_add(&seg_cdev, devno, count);
	if (err < 0)
	{
		printk(KERN_INFO "Error: cdev_add()\n");
		return -1;
	}

	//class�� �����Ѵ�.
	class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(class))
	{
		err = PTR_ERR(class);
		printk(KERN_INFO "class_create error %d\n", err);

		cdev_del(&seg_cdev);
		unregister_chrdev_region(devno, 1);
		return err;
	}

	//��带 �ڵ����� ������ش�.
	dev = device_create(class, NULL, devno, NULL, DEVICE_NAME);
	if (IS_ERR(dev))
	{
		err = PTR_ERR(dev);
		printk(KERN_INFO "device create error %d\n", err);
		class_destroy(class);
		cdev_del(&seg_cdev);
		unregister_chrdev_region(devno, 1);
		return err;
	}

	//printk(KERN_INFO "'sudo mknod /dev/%s c %d 0'\n", DEVICE_NAME, SEG_MAJOR);
	printk(KERN_INFO "'sudo chmod 666 /dev/%s'\n", DEVICE_NAME);

	pr_info("HR Timer module installing\n");

	//===============================================
	// ktime�� ns�� �⺻ ������ ����Ͽ� 10ms�� 
	// 10^6�� �����ش�.
	// * ktime = ktime_set(0, 100 * 1000 * 1000);
	// * 10 ms = 10 * 1000 * 1000 ns
	//===============================================	 
	// 10ms �ð��� ktime�� ����
	ktime = ktime_set(0, MS_TO_NS(delay_in_ms));

	//hr_timer�� �ʱ�ȭ
	hrtimer_init(&hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	//hr_timer�� Ÿ�� ����� ȣ���� �Լ� ���� 
	hr_timer.function = &my_hrtimer_callback;
	pr_info("Starting timer to fire in %ldms (%ld)\n", delay_in_ms, jiffies);

	//hr_timer ����
	hrtimer_start(&hr_timer, ktime, HRTIMER_MODE_REL);

	return 0;
}

static void __exit cleanupModule(void)
{
	int ret;
	dev_t devno = MKDEV(SEG_MAJOR, SEG_MINOR);

	// 0.hrTimer cancel
	ret = hrtimer_cancel(&hr_timer);
	if (ret)
		pr_info("The timer was still in use...\n");

	pr_info("HR Timer module uninstalling\n");

	// 1.���� ����̽��� ����� �����Ѵ�.
	unregister_chrdev_region(devno, 1);

	// 2.���� ����̽��� ����ü�� �����Ѵ�.
	cdev_del(&seg_cdev);
	
	printk("Good-bye!\n");
}


//sudo insmod ȣ��Ǵ� �Լ��� ����
module_init(initModule);

//sudo rmmod ȣ��Ǵ� �Լ��� ����
module_exit(cleanupModule);

//����� ����
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIO Driver Module");
MODULE_AUTHOR("Heejin Park");

