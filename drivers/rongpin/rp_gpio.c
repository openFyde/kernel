#include <linux/module.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/wakelock.h>
#include <asm/uaccess.h>

struct rp_gpio {
	int gpio_num;		//gpui num
	int action;		//gpio flag
        int gpio_event;		//input only
        int send_mode;		//input only
	int gpio_function;	//gpio function,i/o
	int gpio_ctrl;
};

struct rp_gpio_data {
	struct rp_gpio rp_gpio_num[20];
	struct input_dev *input;
	struct timer_list mytimer;
	int gpio_dts_num;
};

static struct rp_gpio_data *gpio_data;
static int event_flag = 0;
static int open_now = 0;
static int gpio_cnt = 0;


static int gpio_open(struct inode *inode, struct file *file)
{
	struct dentry* dent = file->f_path.dentry;
	char* name = NULL;
	int name_int = 0;

	name = (char*)(dent->d_name.name);
	name = name + 4;
	open_now = simple_strtol(name,NULL,5);

	return 0;
}


static int gpio_write(struct file * file,const char __user* buffer,size_t count,loff_t *data)
{
	char buf[2]={0};
	char s1[]="1";
	
	if(copy_from_user(&buf[0],buffer,1)){
                printk("failed to copy data to kernel space\n");
            return -EFAULT;     
        }

	if(!strcmp(buf,s1)){
		gpio_direction_output(gpio_data->rp_gpio_num[open_now].gpio_num,1);
		printk("gpio%d write 1",open_now);
	}else{	
		gpio_direction_output(gpio_data->rp_gpio_num[open_now].gpio_num,0);
		printk("gpio%d write 0 succeed",open_now);
	}
	return count;
}


static int gpio_read(struct file *file, char __user *buf,size_t count, loff_t *ppos)
{
	int gpio_val = 0;
	int len;
	char s[32] = {0};

	gpio_val  = gpio_get_value(gpio_data->rp_gpio_num[open_now].gpio_num);
    printk("gpio_read gpio%d value %d\n",open_now,gpio_val);

	//len = sprintf(s+len, "%d",gpio_val);
	len = sprintf(s, "%d",gpio_val);
	copy_to_user(buf,s,len);
	return 0;
}


static const struct file_operations gpio_ops = {
	.owner          = THIS_MODULE,
    .open           = gpio_open,
    .write          = gpio_write,
    .read           = gpio_read,
};

static void send_event(unsigned long data) 
{
	int gpio_value = 0;
	int i = 0 ;
	for(i = 0; i <= gpio_data->gpio_dts_num; i++)
	{
		if(gpio_data->rp_gpio_num[i].gpio_function == 1)
		{
			gpio_value  = gpio_get_value(gpio_data->rp_gpio_num[i].gpio_num);
			if (gpio_data->rp_gpio_num[i].send_mode == 0) {
 				if (!(gpio_data->rp_gpio_num[i].action) == gpio_value) {
                			input_report_key(gpio_data->input, gpio_data->rp_gpio_num[i].gpio_event, 1);
                			input_sync(gpio_data->input);
                			mdelay(1);
                			input_report_key(gpio_data->input, gpio_data->rp_gpio_num[i].gpio_event, 0);
                			input_sync(gpio_data->input);
        			}
			} else if (gpio_data->rp_gpio_num[i].send_mode == 1){
       	 			if(gpio_value != event_flag){
                			input_report_key(gpio_data->input, gpio_data->rp_gpio_num[i].gpio_event, 1);
                			input_sync(gpio_data->input);
                			mdelay(1);
                			input_report_key(gpio_data->input, gpio_data->rp_gpio_num[i].gpio_event, 0);
                			input_sync(gpio_data->input);
                			event_flag = gpio_value;
        			}
			}
	//		printk("\n%s gpio num %d  %d\n",__func__,gpio_data->rp_gpio_num[i].gpio_num,gpio_value);
	//	        printk("\n%s send event %d\n",__func__,gpio_data->rp_gpio_num[i].gpio_event);
		}
	}
	
        mod_timer(&(gpio_data->mytimer), jiffies + msecs_to_jiffies(200));
        return;
}


static ssize_t hdmi_in_show(struct device *dev, struct device_attribute *attr, char *buf){
    int gpio_val = 0;
	int len;

	gpio_val  = gpio_get_value(gpio_data->rp_gpio_num[open_now].gpio_num);
    //printk("get gpio%d value %d\n",open_now,gpio_val);

	len = sprintf(buf, "%d",gpio_val);
    
	return len;
}


static struct device_attribute hdmi_in_attr[] = {
	__ATTR_MY(hdmi_in, S_IRWXUGO, hdmi_in_show, NULL),
};

static void gpio_init_sysfs(struct platform_device *pdev)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(hdmi_in_attr); i++) {
		ret = sysfs_create_file(&(pdev->dev.kobj),
					&hdmi_in_attr[i].attr);
		if (ret){
			printk("rp_gpio_ create rp_gpio_attr node(%s) error\n",	hdmi_in_attr[i].attr.name);
        }
	}
}


static int rp_gpio_probe(struct platform_device *pdev)
{

    struct device_node *np = pdev->dev.of_node;
	struct device_node *child_np;
	struct device *dev = &pdev->dev;
	static struct proc_dir_entry *root_entry_gpio;
	enum of_gpio_flags  gpio_flags;
	int ret = 0;
	int gpio_cnt = 0;	
	char gpio_name_num[20];
	int gpio_np_num = 0;

	gpio_np_num = of_get_child_count(np);		//get dts gpio number
         if (gpio_np_num == 0)
                 dev_info(&pdev->dev, "no gpio defined\n");


	gpio_data = devm_kzalloc(&pdev->dev, sizeof(struct rp_gpio_data),GFP_KERNEL);
	if (!gpio_data) {
                dev_err(&pdev->dev, "failed to allocate memory\n");
                return -ENOMEM;
        }
	
	gpio_data->gpio_dts_num = gpio_np_num;

        /* create node */
        root_entry_gpio = proc_mkdir("rp_gpio", NULL);

	for_each_child_of_node(np, child_np)
	{
		/* parse dts */
		gpio_data->rp_gpio_num[gpio_cnt].gpio_num = of_get_named_gpio_flags(child_np, "gpio_num", 0, &gpio_flags);

		if (!gpio_is_valid(gpio_data->rp_gpio_num[gpio_cnt].gpio_num))
			gpio_data->rp_gpio_num[gpio_cnt].gpio_num = -1;
		
		gpio_data->rp_gpio_num[gpio_cnt].action = gpio_flags;
		if(gpio_data->rp_gpio_num[gpio_cnt].gpio_num != -1)
		{
			gpio_data->rp_gpio_num[gpio_cnt].gpio_ctrl = gpio_cnt;
			of_property_read_u32(child_np, "gpio_function", &(gpio_data->rp_gpio_num[gpio_cnt].gpio_function));

			if (gpio_data->rp_gpio_num[gpio_cnt].gpio_function == 1)
			{
				ret = gpio_request(gpio_data->rp_gpio_num[gpio_cnt].gpio_num, "gpio_num");
				if (ret < 0)
				{
					printk("data->gpio_int request error\n");
					return ret;
				}else
				{
					gpio_direction_input(gpio_data->rp_gpio_num[gpio_cnt].gpio_num);
                    printk("\n%s gpio_direction_input(): gpio num %d  \n",__func__, gpio_data->rp_gpio_num[gpio_cnt].gpio_num);
				}
				
				event_flag = gpio_flags;
				of_property_read_u32(child_np, "send_mode", &(gpio_data->rp_gpio_num[gpio_cnt].send_mode));
				of_property_read_u32(child_np, "gpio_event", &(gpio_data->rp_gpio_num[gpio_cnt].gpio_event));
                //yinluo add
				if(gpio_data->rp_gpio_num[gpio_cnt].send_mode != 2){
					/* init struct input_dev */ 
					gpio_data->input = devm_input_allocate_device(dev);
					gpio_data->input->name = "gpio_event";      /* pdev->name; */
					gpio_data->input->phys = "gpio_event/input1";
					gpio_data->input->dev.parent = dev;
					gpio_data->input->id.bustype = BUS_HOST;
					gpio_data->input->id.vendor = 0x0001;
					gpio_data->input->id.product = 0x0001;
					gpio_data->input->id.version = 0x0100;
					input_set_capability(gpio_data->input, EV_KEY, gpio_data->rp_gpio_num[gpio_cnt].gpio_event);
					input_register_device(gpio_data->input);

					/* init timer */
					init_timer(&(gpio_data->mytimer));
					gpio_data->mytimer.expires = jiffies + msecs_to_jiffies(10000);
					gpio_data->mytimer.function = send_event;
					add_timer(&(gpio_data->mytimer));
                }
			} else {
				ret = gpio_request(gpio_data->rp_gpio_num[gpio_cnt].gpio_num, "gpio_num");
				if (ret < 0){
					printk("data->gpio_int request error\n");
					return ret;
				}else{
					gpio_direction_output(gpio_data->rp_gpio_num[gpio_cnt].gpio_num,!gpio_data->rp_gpio_num[gpio_cnt].action);
					printk("\n%s gpio_direction_output(): gpio num %d  \n",__func__, gpio_data->rp_gpio_num[gpio_cnt].gpio_num);
                }
			}
			sprintf(gpio_name_num,"gpio%d",gpio_cnt);
			proc_create(gpio_name_num, 0666 , root_entry_gpio , &gpio_ops);
		}
		gpio_cnt++;
	}

	platform_set_drvdata(pdev, gpio_data);
    gpio_init_sysfs(pdev);
    printk("\n%s probe success! \n", __func__);

    return 0;
}

static int rp_gpio_remove(struct platform_device *pdev)
{
    struct rp_gpio_data *data = platform_get_drvdata(pdev);
    return 0;
}

#ifdef CONFIG_PM 
static int rp_gpio_suspend(struct device *dev) 
{ 
	struct platform_device *pdev = to_platform_device(dev);
    struct rp_gpio_data *data = platform_get_drvdata(pdev);
        
    printk("%s\n",__func__);
 
	del_timer(&(gpio_data->mytimer));
 
    return 0; 
} 
 
static int rp_gpio_resume(struct device *dev) 
{ 
	struct platform_device *pdev = to_platform_device(dev);
    struct rp_gpio_data *data = platform_get_drvdata(pdev);
    printk("%s\n",__func__);
	add_timer(&(gpio_data->mytimer));
    return 0; 
} 
 
static const struct dev_pm_ops rp_gpio_pm_ops = { 
    .suspend        = rp_gpio_suspend, 
    .resume         = rp_gpio_resume, 
}; 
#endif

static const struct of_device_id rp_gpio_of_match[] = {
    { .compatible = "rp_gpio" },
    { }
};

static struct platform_driver rp_gpio_driver = {
    .probe = rp_gpio_probe,
    .remove = rp_gpio_remove,
    .driver = {
                .name           = "rp_gpio",
                .of_match_table = of_match_ptr(rp_gpio_of_match),
#ifdef CONFIG_PM
                .pm     = &rp_gpio_pm_ops,
#endif

        },
};

module_platform_driver(rp_gpio_driver);
MODULE_LICENSE("GPL");
