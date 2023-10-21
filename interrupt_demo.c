#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/sched/signal.h>
#include <asm/siginfo.h>
#include <linux/device.h>
#include<linux/slab.h>                 //kmalloc()
#include<linux/uaccess.h>              //copy_to/from_user()
#include <asm/io.h>

#define GPIO_20_OUT (20)
#define GPIO_21_IN  (21)
#define TIMEOUT_MS  3000
#define SIG_GPIO    18

#define REG_SIG_GPIO _IOW('s', 1, int32_t*)

static struct task_struct *task = NULL;
static int timer_run = 0;
static struct timer_list timer;
static u8 state = 0;
static char cmd[2] = {0};
static int GPIO_21_IN_IRQ_NUM;
extern unsigned long volatile jiffies;
static unsigned long old_jiffie = 0;
struct tasklet_struct * my_tasklet = NULL;

void tasklet_fn(unsigned long data)
{
    struct kernel_siginfo info;
    memset(&info, 0, sizeof(struct kernel_siginfo));
    info.si_signo = SIG_GPIO;
    info.si_code = SI_QUEUE;
    info.si_int = 1;
 
    if (task != NULL) {
        pr_info("Sending signal to app\n");
        if(send_sig_info(SIG_GPIO, &info, task) < 0) 
        {
            pr_err("Unable to send signal\n");
        }
    }
}

void timer_callback(struct timer_list * data)
{
    state = !state;
    pr_info("state = %d\n", state);
    gpio_set_value(GPIO_20_OUT, state);
    //reenable time
    mod_timer(&timer, jiffies + msecs_to_jiffies(TIMEOUT_MS));
}

static int misc_open(struct inode *inode, struct file *file)
{
    pr_info("misc device open\n");
    return 0;
}

static int misc_release(struct inode *inode, struct file *file)
{
    task = NULL;
    return 0;
}

static int misc_close(struct inode *inodep, struct file *file)
{
    pr_info("misc device close\n");
    return 0;
}

static ssize_t misc_read(struct file *filp, char __user *buf,
                    size_t count, loff_t *f_pos)
{
    pr_info("misc device read\n");
    return 0;
}

static ssize_t misc_write(struct file *file, const char __user *buf,
               size_t len, loff_t *ppos)
{
    pr_info("misc device write\n");
    
    copy_from_user(cmd, buf, len);
    if(len > 1) cmd[1] = 0;
    pr_info("cmd = %s\n", cmd);
    if(timer_run == 1)
    {
        del_timer(&timer);
        timer_run = 0;
    }
    if(cmd[0] == '1')
    {
        timer_run = 1;
        timer_setup(&timer, timer_callback, 0);
        mod_timer(&timer, jiffies + msecs_to_jiffies(TIMEOUT_MS));
        //sudo bash -c 'echo "1" > /dev/misc_dev'
    }
    return len; 
}

static long misc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    switch(cmd) {
        case REG_SIG_GPIO:
            task = get_current();
            pr_info("REG_SIG_GPIO");
            break;
        default:
            break;
    }
    return 0;
}

static const struct file_operations fops = {
    .owner          = THIS_MODULE,
    .write          = misc_write,
    .read           = misc_read,
    .open           = misc_open,
    .release        = misc_close,
    .unlocked_ioctl = misc_ioctl,
    .release        = misc_release,
};

//Misc device structure
struct miscdevice misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "misc_dev",
    .fops = &fops,
};
static irqreturn_t gpio_irq_handler(int irq,void *dev_id) 
{
    //debounce
    unsigned long diff = jiffies - old_jiffie;
    if (diff < 20)
    {
        return IRQ_HANDLED;
    }
    old_jiffie = jiffies; 

    tasklet_schedule(my_tasklet);

    return IRQ_HANDLED;
}

static int init_gpio(void)
{
    int ret;
    if(gpio_is_valid(GPIO_20_OUT) == false){
        pr_err("GPIO %d is not valid\n", GPIO_20_OUT);
        return -1;
    }

    if(gpio_request(GPIO_20_OUT,"GPIO_20_OUT") < 0)
    {
        pr_err("ERROR: GPIO %d request\n", GPIO_20_OUT);
        return -1;
    }

    gpio_direction_output(GPIO_20_OUT, 0);
  
    if(gpio_is_valid(GPIO_21_IN) == false){
        pr_err("GPIO %d is not valid\n", GPIO_21_IN);
        return -1;
    }
    if(gpio_request(GPIO_21_IN,"GPIO_21_IN") < 0){
        pr_err("ERROR: GPIO %d request\n", GPIO_21_IN);
        return -1;
    }
    gpio_direction_input(GPIO_21_IN);

    GPIO_21_IN_IRQ_NUM = gpio_to_irq(GPIO_21_IN);
    pr_info("GPIO_21_IN_IRQ_NUM = %d\n", GPIO_21_IN_IRQ_NUM);

    ret = request_irq(GPIO_21_IN_IRQ_NUM,             //IRQ number
                    (irq_handler_t)gpio_irq_handler,   //IRQ handler
                    IRQF_TRIGGER_RISING,        //Handler will be called in raising edge
                    "gpio_dev",               //used to identify the device name using this IRQ
                    NULL);
    pr_err("ret register IRQ %d", ret);
    if (ret < 0) 
    {                 
        pr_err("cannot register IRQ %d", ret);
        return -1;
    }
    return 0;
}

void free_gpio(void)
{
    free_irq(GPIO_21_IN_IRQ_NUM, NULL);
    gpio_free(GPIO_20_OUT);
    gpio_free(GPIO_21_IN);
}

static int __init misc_init(void)
{
    int error;
 
    error = misc_register(&misc_device);
    if (error) 
    {
        pr_err("misc_register failed!!!\n");
        return error;
    }
    init_gpio();
    //
    my_tasklet  = kmalloc(sizeof(struct tasklet_struct),GFP_KERNEL);
    if(my_tasklet == NULL) 
    {
        pr_info("cannot allocate tasklet");
        return -1;
    }
    tasklet_init(my_tasklet, tasklet_fn, 0);

    pr_info("misc init\n");
    return 0;
}

static void __exit misc_exit(void)
{
    if(timer_run)
    {
        del_timer(&timer);
    }
    free_gpio();
    tasklet_kill(my_tasklet);
    misc_deregister(&misc_device);
    
    pr_info("misc exit\n");
}

module_init(misc_init)
module_exit(misc_exit)
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("nha.tuan84@gmail.com");
MODULE_DESCRIPTION("Misc Device");
MODULE_VERSION("0.1");