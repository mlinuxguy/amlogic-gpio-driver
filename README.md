# amlogic-gpio-driver
Attempt at a device-tree / platform version of the odroid-c1 gpio driver
Please note that this driver builds and works did the original driver.
This driver needs routines added so linux can handle the IRQ in sysfs:
static struct irq_chip gpio_irq_chip = {
    .name       = "GPIO",
    .irq_shutdown   = gpio_irq_shutdown,
    .irq_ack    = gpio_ack_irq,
    .irq_mask   = gpio_mask_irq,
    .irq_unmask = gpio_unmask_irq,
    .irq_set_type   = gpio_irq_type,
    .irq_set_wake   = gpio_wake_enable,
};

This still gives the error during "echo both > /sys/class/gpio/gpio97/edge" that the original driver did, but its
closer to working with sysfs.
