#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/gpio/driver.h>
#include <linux/slab.h>

#define GPFSEL0 0x00  // Function select
#define GPSET0  0x1C  // Set pin
#define GPCLR0  0x28  // Clear pin
#define GPLEV0  0x34  // Pin level

struct custom_gpio {
	void __iomem *base;
	struct gpio_chip chip;
};

static void custom_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	dev_info(chip->parent, "GPIO SET called: offset=%u, value=%d\n", offset, value);

	struct custom_gpio *cg = gpiochip_get_data(chip);
    u32 reg = offset / 32;
    u32 shift = offset % 32;
    void __iomem *set_clr_reg = NULL;
	if (value){
        set_clr_reg = cg->base + GPSET0 + (reg * 4);
    }
	else{
        set_clr_reg = cg->base + GPCLR0 + (reg * 4);
    }
    writel((0x1 << shift), set_clr_reg);
}

static int custom_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct custom_gpio *cg = gpiochip_get_data(chip);
    u32 reg = offset / 32;
    u32 shift = offset % 32;
    void __iomem *lev_reg = cg->base + GPLEV0 + (reg * 4);
	u32 val = readl(lev_reg);

	return !!(val & (0x1 << shift));
}

static int custom_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct custom_gpio *cg = gpiochip_get_data(chip);
	u32 reg = offset / 10;
	u32 shift = (offset % 10) * 3;
	void __iomem *fsel_reg = cg->base + GPFSEL0 + (reg * 4);
	u32 val = readl(fsel_reg);

	val &= ~(0x7 << shift); // clear bits
	writel(val, fsel_reg);

	return 0;
}

static int custom_gpio_direction_output(struct gpio_chip *chip, unsigned int offset, int value)
{
	dev_info(chip->parent, "GPIO DIR OUT called: offset=%u, value=%d\n", offset, value);

	struct custom_gpio *cg = gpiochip_get_data(chip);
	u32 reg = offset / 10;
	u32 shift = (offset % 10) * 3;
	void __iomem *fsel_reg = cg->base + GPFSEL0 + (reg * 4);
	u32 val = readl(fsel_reg);

	val &= ~(0x7 << shift);       // clear bits
	val |= (0x1 << shift);        // set as output
	writel(val, fsel_reg);

	custom_gpio_set(chip, offset, value);

	return 0;
}

static int custom_gpio_probe(struct platform_device *pdev)
{
	struct custom_gpio *cg;
	struct resource *res;
	int ret;

	dev_info(&pdev->dev, "Custom GPIO driver loaded\n");

	cg = devm_kzalloc(&pdev->dev, sizeof(*cg), GFP_KERNEL);
	if (!cg)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cg->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(cg->base))
		return PTR_ERR(cg->base);

	cg->chip.label = "custom-gpio";
	cg->chip.parent = &pdev->dev;
	cg->chip.owner = THIS_MODULE;
	cg->chip.base = -1;
	cg->chip.ngpio = 54;
	cg->chip.set = custom_gpio_set;
	cg->chip.get = custom_gpio_get;
	cg->chip.direction_input = custom_gpio_direction_input;
	cg->chip.direction_output = custom_gpio_direction_output;
	cg->chip.can_sleep = false;

	ret = devm_gpiochip_add_data(&pdev->dev, &cg->chip, cg);

	if (ret) {
		dev_err(&pdev->dev, "Failed to add gpio chip: %d\n", ret);
	} else{
		dev_info(&pdev->dev, "GPIO chip registered!\n");
	}

	return ret;
}

static const struct of_device_id custom_gpio_of_match[] = {
	{ .compatible = "puda,custom-bcm2835-gpio-driver" },
	{ }
};
MODULE_DEVICE_TABLE(of, custom_gpio_of_match);

static struct platform_driver custom_gpio_driver = {
	.probe = custom_gpio_probe,
	.driver = {
		.name = "custom-bcm2835-gpio",
		.of_match_table = custom_gpio_of_match,
	},
};

module_platform_driver(custom_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Puda");
MODULE_DESCRIPTION("Minimal BCM2835 GPIO driver with gpio_chip API");
