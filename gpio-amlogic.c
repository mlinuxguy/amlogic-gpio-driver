/*
 * Driver for the AMLOGIC  GPIO
 *
 * Copyright (c) AMLOGIC CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/consumer.h>
#include <mach/io.h>
#include <plat/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/amlogic/gpio-amlogic.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/gpio.h>
static DEFINE_SPINLOCK(gpio_irqlock);

static const struct irq_domain_ops amlogic_irqd_ops = {
		.xlate	= irq_domain_xlate_onecell,
};

struct amlogic_gpio_reg_offs {
		u16 revision;
};

struct amlogic_gpio_platform_data {
	struct irq_domain *domain;
	struct amlogic_gpio_reg_offs *regs;
};

struct amlogic_gpio_reg_offs amlogic_gpio_regs = {
		.revision = 0x01,
};

#define MDEBUG 1 // debug printfs

struct amlogic_set_pullup pullup_ops;
extern struct amlogic_gpio_desc amlogic_pins[];
extern int gpio_amlogic_name_to_num(const char *name);
#define pin_to_name(pin) (amlogic_pins[pin].name)
int gpio_irq;
int gpio_flag;
int gpio_range_check(unsigned int  pin)
{
	if(pin>=GPIO_MAX){
		printk("GPIO:%d out of gpio range!!!\n",pin);
		return -1;
	}
	else
		return 0;
}
static void set_gpio_owner(unsigned int  pin,const char * owner)
{
	amlogic_pins[pin].gpio_owner=owner;	
}

/* amlogic request gpio interface*/

int amlogic_gpio_request(unsigned int  pin,const char *label)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	ret=gpio_request(pin, label);
	if(!ret)
	{
		set_gpio_owner(pin,label);
		return ret;
	}
	if (ret==-EBUSY)
	{
		printk("%s is using the pin %s\n",amlogic_pins[pin].gpio_owner,pin_to_name(pin));
		return ret;
	}
	return ret;
}
EXPORT_SYMBOL(amlogic_gpio_request);

int amlogic_gpio_request_one(unsigned pin, unsigned long flags, const char *label)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	ret=gpio_request_one(pin,flags, label);
	if(!ret)
	{
		set_gpio_owner(pin,label);
		return ret;
	}
	if (ret==-EBUSY)
	{
		printk("%s is using the pin %s\n",amlogic_pins[pin].gpio_owner,pin_to_name(pin));
		return ret;
	}
	return ret;

}
EXPORT_SYMBOL(amlogic_gpio_request_one);

int amlogic_gpio_request_array(const struct gpio *array, size_t num)
{
	int i, err;
	for (i = 0; i < num; i++, array++) {
		err = amlogic_gpio_request_one(array->gpio, array->flags, array->label);
		if (err)
			goto err_free;
	}
	return 0;

err_free:
	while (i--)
		gpio_free((--array)->gpio);
	return err;
}
EXPORT_SYMBOL(amlogic_gpio_request_array);

int amlogic_gpio_free_array(const struct gpio *array, size_t num)
{
	int ret=0;
	while (num--){
		ret=amlogic_gpio_free(array->gpio,array->label);
		if(ret)
			return ret;
		array++;
	}
	return ret;
}
EXPORT_SYMBOL(amlogic_gpio_free_array);


int amlogic_gpio_direction_input(unsigned int pin,const char *owner)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner)
		if(!strcmp(amlogic_pins[pin].gpio_owner,owner))
			ret=gpio_direction_input(pin);	
	return ret;
}
EXPORT_SYMBOL(amlogic_gpio_direction_input);

int amlogic_gpio_direction_output(unsigned int pin,int value,const char *owner)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner)
		if(!strcmp(amlogic_pins[pin].gpio_owner,owner))
			ret=gpio_direction_output(pin,value);
	return ret;
}
EXPORT_SYMBOL(amlogic_gpio_direction_output);

const char * amlogic_cat_gpio_owner(unsigned int pin)
{
	if(gpio_range_check(pin))
		return NULL;
	return amlogic_pins[pin].gpio_owner;
}
EXPORT_SYMBOL(amlogic_cat_gpio_owner);

/* amlogic free gpio interface*/

int amlogic_gpio_free(unsigned int  pin,const char * owner)
{
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner){
		if(!strcmp(owner,amlogic_pins[pin].gpio_owner))
		{
			gpio_free(pin);
			amlogic_pins[pin].gpio_owner=NULL;
			return 0;
		}else{
			printk("%s try to free gpio %s, but the gpio %s owner is %s",owner,amlogic_pins[pin].name,
						amlogic_pins[pin].name,amlogic_pins[pin].gpio_owner);
			return -1;
		}
	}
	return -1;
}
EXPORT_SYMBOL(amlogic_gpio_free);

/* amlogic  gpio to irq interface*/

int amlogic_request_gpio_to_irq(unsigned int  pin,const char *label,unsigned int flag)
{
	int ret=-1;
	unsigned long flags;
	if(gpio_range_check(pin))
		return -1;
	ret=amlogic_gpio_request(pin, label);
#ifdef MDEBUG
	    printk("aml req gpio to irq request -> ret=%x\n", ret);
#endif
	if(!ret)
	{	
		spin_lock_irqsave(&gpio_irqlock, flags);
		gpio_flag=flag;
		__gpio_to_irq(pin);
		spin_unlock_irqrestore(&gpio_irqlock, flags);
	}
	return ret;
}
EXPORT_SYMBOL(amlogic_request_gpio_to_irq);

int amlogic_gpio_to_irq(unsigned int  pin,const char *owner,unsigned int flag)
{
	int ret=-1;
	unsigned long flags;
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner)
#ifdef MDEBUG
			        printk("aml gpio to irq passed as owner\n");
#endif
		if(!strcmp(amlogic_pins[pin].gpio_owner,owner))
		{
#ifdef MDEBUG
            printk("aml gpio to irq: owner check for pin: %d\n",pin);
#endif
			spin_lock_irqsave(&gpio_irqlock, flags);
			gpio_flag=flag;
			__gpio_to_irq(pin);
			spin_unlock_irqrestore(&gpio_irqlock, flags);
			return 0;
		}
	return ret;
}
EXPORT_SYMBOL(amlogic_gpio_to_irq);

int amlogic_get_value(unsigned int pin,const char *owner)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner)
		if(!strcmp(amlogic_pins[pin].gpio_owner,owner))
			return gpio_get_value(pin);
	return ret;
}
EXPORT_SYMBOL(amlogic_get_value);

int amlogic_set_value(unsigned int pin,int value,const char *owner)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner)
		if(!strcmp(amlogic_pins[pin].gpio_owner,owner)){
			gpio_set_value(pin,value);
			return 0;
		}
	return ret;
}
EXPORT_SYMBOL(amlogic_set_value);

int amlogic_gpio_name_map_num(const char *name)
{
	return gpio_amlogic_name_to_num(name);
}
EXPORT_SYMBOL(amlogic_gpio_name_map_num);
int amlogic_set_pull_up_down(unsigned int pin,unsigned int val,const char *owner)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner)
		if(pullup_ops.meson_set_pullup){
			pullup_ops.meson_set_pullup(pin,val,1);
			return 0;
		}
	return ret;
}
EXPORT_SYMBOL(amlogic_set_pull_up_down);
int amlogic_disable_pullup(unsigned int pin,const char *owner)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner)
		if(pullup_ops.meson_set_pullup){
			pullup_ops.meson_set_pullup(pin,0xffffffff,0);
			return 0;
		}
	return ret;
}
EXPORT_SYMBOL(amlogic_disable_pullup);
/* --------- end of old driver ------- */
static const struct of_device_id amlogic_gpio_match[];
/* This lock class tells lockdep that GPIO irqs are in a different
 *  * category than their parents, so it won't report false recursion.
 *   */
static struct lock_class_key gpio_lock_class;

static int amlogic_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	const struct of_device_id *match;
	struct amlogic_gpio_platform_data *pdata;
	struct resource *res;
	int i;
#ifdef MDEBUG
	printk("amlogic_gpio_probe called\n");
#endif
	match = of_match_device(of_match_ptr(amlogic_gpio_match), dev);
	pdata = match ? match->data : dev->platform_data;
	if (!pdata) return -EINVAL;
    res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
    if (unlikely(!res)) {
        dev_err(dev, "Invalid IRQ resource\n");
        return -ENODEV;
    }
	pdata->domain = irq_domain_add_linear(node, 8, &amlogic_irqd_ops, NULL);
    if (!pdata->domain) {
       dev_err(dev, "Couldn't register an IRQ domain\n");
       return -ENODEV;
    }
	for (i = 0; i < 8; i++) {
		int irq = irq_create_mapping(pdata->domain,i);
		irq_set_lockdep_class(irq, &gpio_lock_class);
		irq_set_chip_data(irq, pdata);
//		irq_set_chip_and_handler(irq, &gpio_irq_chip, handle_simple_irq);
		set_irq_flags(irq, IRQF_VALID);
	}
	return 0;
}

static const struct amlogic_gpio_platform_data amlogic_pdata = {
	.regs = &amlogic_gpio_regs,
//	.bank_width = 32,
//	.dbck_flag = true,
};

static const struct of_device_id amlogic_gpio_match[] = {
	{
	.compatible = "amlogic,m8b-gpio",
	.data = &amlogic_pdata,
	},
	{ },
};

MODULE_DEVICE_TABLE(of, amlogic_gpio_match);

static struct platform_driver amlogic_gpio_driver = {
	.probe      = amlogic_gpio_probe,
	.driver     = {
		.name   = "m8b-gpio",
//		.pm = &gpio_pm_ops,
		.of_match_table = of_match_ptr(amlogic_gpio_match),
	},
};

/*
 *  * gpio driver register needs to be done before
 *   * machine_init functions access gpio APIs.
 *    * Hence amlogic_gpio_drv_reg() is a postcore_initcall.
 *     */
static int __init amlogic_gpio_drv_reg(void)
{
		    return platform_driver_register(&amlogic_gpio_driver);
}
postcore_initcall(amlogic_gpio_drv_reg);
