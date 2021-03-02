#include <linux/module.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/sysfs.h>

static struct timer_list mytimer;
static struct timer_list spkLedtimer;


struct led_gpio {
	int gpio_num;
	int val;
    int available;
};
struct led_led_gpio *led_data;

struct led_led_gpio {
	struct led_gpio breathe_led;
    struct led_gpio mic_led;  //mic mute led
    struct led_gpio spk_led;  //spk  mute led
};
static int spk_led_flash_on = 0;
static ssize_t spk_led_show(struct device *dev, struct device_attribute *attr, char *buf){
	int value = -1;
	if(led_data->spk_led.available > 0){
		value = gpio_get_value(led_data->spk_led.gpio_num);
        sprintf(buf, "%d\n", value);
		value = strlen(buf) + 1;
    }
	return value;
}

void spk_led_timer_function(unsigned long data){
		if(spk_led_flash_on){
	        led_data->spk_led.val ^= 1;
	        gpio_direction_output(led_data->spk_led.gpio_num,led_data->spk_led.val);
        }
        mod_timer(&spkLedtimer, jiffies + msecs_to_jiffies(800));
        return;
}

static ssize_t spk_led_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count){
	int ret = -1;
    int value = -1;
    
	//ret = sscanf(buf, "%d", &value);
    ret = kstrtou32(buf, 10, &value);
	if (ret < 0) {
		printk("led-led error! cmd require only one args\n");
		return ret;
	}

    //led flash
    if(value == 2){
        led_data->spk_led.val = 0; 
        gpio_direction_output(led_data->spk_led.gpio_num,led_data->spk_led.val); 
		spk_led_flash_on = 1;
    }else{
    	spk_led_flash_on = 0;
    	led_data->spk_led.val = value; 
    	gpio_direction_output(led_data->spk_led.gpio_num,led_data->spk_led.val);    
    }
	return count;
}


static ssize_t mic_led_show(struct device *dev, struct device_attribute *attr, char *buf){
	int value = -1;
	if(led_data->mic_led.available > 0){
		value = gpio_get_value(led_data->mic_led.gpio_num);
        sprintf(buf, "%d\n", value);
		value = strlen(buf) + 1;
    }
	return value;
}

static ssize_t mic_led_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count){
    int ret = -1;
    int value = -1;
    //ret = sscanf(buf, "%d", &value);
    ret = kstrtou32(buf, 10, &value);
    if (ret < 0) {
        printk("led-led  error! cmd require only one args\n");
        return ret;
    }

    led_data->mic_led.val = value; //^= 1;
    gpio_direction_output(led_data->mic_led.gpio_num,led_data->mic_led.val);    
    return count;
}

static struct device_attribute leds_attr[] = {
	__ATTR_MY(mic_led, S_IRWXUGO, mic_led_show, mic_led_store),
    __ATTR_MY(spk_led, S_IRWXUGO, spk_led_show, spk_led_store),
};

void function(unsigned long data){
	struct led_led_gpio *tmp = (struct led_led_gpio *)data;

        tmp->breathe_led.val ^= 1;
        gpio_direction_output(tmp->breathe_led.gpio_num,tmp->breathe_led.val);
        mod_timer(&mytimer, jiffies + msecs_to_jiffies(2000));
        return;
}

static void led_init_sysfs(struct platform_device *pdev)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(leds_attr); i++) {
		ret = sysfs_create_file(&(pdev->dev.kobj),
					&leds_attr[i].attr);
		if (ret){
			printk("led-led create leds_attr node(%s) error\n",	leds_attr[i].attr.name);
        }
	}
}


static int led_probe(struct platform_device *pdev)
{
        int ret = 0;
        struct device_node *np = pdev->dev.of_node;
	struct led_led_gpio *data;

	data = devm_kzalloc(&pdev->dev, sizeof(struct led_led_gpio),GFP_KERNEL);
	if (!data) {
        dev_err(&pdev->dev, "failed to allocate memory\n");
        return -ENOMEM;
    }
	memset(data, 0, sizeof(struct led_led_gpio));
	led_data = data;
    
    data->breathe_led.gpio_num = of_get_named_gpio_flags(np, "breathe-led", 0, NULL);
    if (!gpio_is_valid(data->breathe_led.gpio_num)){
            data->breathe_led.gpio_num = -1;
			data->breathe_led.available = -1;
    }

    data->mic_led.gpio_num = of_get_named_gpio_flags(np, "mic-mute-led", 0, NULL);
    if (!gpio_is_valid(data->mic_led.gpio_num)){
            data->mic_led.gpio_num = -1;
			data->mic_led.available = -1;
    }

    data->spk_led.gpio_num = of_get_named_gpio_flags(np, "spk-mute-led", 0, NULL);
    if (!gpio_is_valid(data->spk_led.gpio_num)){
            data->spk_led.gpio_num = -1;
            data->spk_led.available = -1;
    }

	platform_set_drvdata(pdev, data);

	if(data->breathe_led.gpio_num != -1){
		ret = gpio_request(data->breathe_led.gpio_num, "breathe-led");
        if (ret < 0){
            data->breathe_led.available = -1;
			printk("led-led breathe-led request error\n");
        	        return ret;
		}else{
			gpio_direction_output(data->breathe_led.gpio_num, 0);
        	gpio_set_value(data->breathe_led.gpio_num, 0);
			data->breathe_led.val = 0;
            data->breathe_led.available = 1;
		}
	}

    if(data->mic_led.gpio_num != -1){
		ret = gpio_request(data->mic_led.gpio_num, "mic-mute-led");
        if (ret < 0){
            data->mic_led.available = -1;
			printk("led-led mic-mute-led request error\n");
        	        return ret;
		}else{
			#ifdef CONFIG_LOGO_LENOVO_CLUT224
	            gpio_direction_output(data->mic_led.gpio_num, 0);
	        	gpio_set_value(data->mic_led.gpio_num, 0);
				data->mic_led.val = 0;
			#else                 
				gpio_direction_output(data->mic_led.gpio_num, 1);
	        	gpio_set_value(data->mic_led.gpio_num, 1);
				data->mic_led.val = 1;
            #endif
            data->mic_led.available = 1;
		}
	}

    if(data->spk_led.gpio_num != -1){
		ret = gpio_request(data->spk_led.gpio_num, "spk-mute-led");
        if (ret < 0){
            data->spk_led.available = -1;
			printk("led-led spk-mute-led request error\n");
        	        return ret;
		}else{
			printk("led-led data->spk_led.gpio_num=%d\n",data->spk_led.gpio_num);
			gpio_direction_output(data->spk_led.gpio_num, 0);
        	gpio_set_value(data->spk_led.gpio_num, 0);
			data->spk_led.val = 0;
            data->spk_led.available = 1;
		}
	}

    led_init_sysfs(pdev);
#if 0    
	init_timer(&mytimer);
	mytimer.expires = jiffies + jiffies_to_msecs(2000);
	mytimer.function = function;
	mytimer.data = (unsigned long )data;
	add_timer(&mytimer);
#endif

#if 0    
    init_timer(&spkLedtimer);
	spkLedtimer.expires = jiffies + jiffies_to_msecs(800);
	spkLedtimer.function = spk_led_timer_function;
	spkLedtimer.data = (unsigned long )data;
	add_timer(&spkLedtimer);    
#endif
    return 0;
}

static int led_remove(struct platform_device *pdev)
{
        struct led_led_gpio *data = platform_get_drvdata(pdev);

	if(data->breathe_led.gpio_num != -1){
		gpio_direction_output(data->breathe_led.gpio_num, 1);
		gpio_free(data->breathe_led.gpio_num);
	}

        return 0;
}

#ifdef CONFIG_PM 
static int led_suspend(struct device *dev) 
{ 
	struct platform_device *pdev = to_platform_device(dev);
        struct led_led_gpio *data = platform_get_drvdata(pdev);
 
	del_timer(&mytimer);
	if(data->breathe_led.val == 0){
		gpio_direction_output(data->breathe_led.gpio_num,1);
		data->breathe_led.val = 1;
	}
 
        return 0; 
} 
 
static int led_resume(struct device *dev) 
{ 
	struct platform_device *pdev = to_platform_device(dev);
        struct led_led_gpio *data = platform_get_drvdata(pdev);
 
	gpio_direction_output(data->breathe_led.gpio_num,0);
	data->breathe_led.val = 0;
	add_timer(&mytimer);
        return 0; 
} 
 
static const struct dev_pm_ops led_pm_ops = { 
        .suspend        = led_suspend, 
        .resume         = led_resume, 
}; 
#endif

static const struct of_device_id led_of_match[] = {
        { .compatible = "led-led" },
        { }
};

static struct platform_driver led_driver = {
        .probe = led_probe,
        .remove = led_remove,
        .driver = {
                .name           = "led-led",
                .of_match_table = of_match_ptr(led_of_match),
#ifdef CONFIG_PM
                .pm     = &led_pm_ops,
#endif

        },
};

module_platform_driver(led_driver);

MODULE_LICENSE("GPL");
