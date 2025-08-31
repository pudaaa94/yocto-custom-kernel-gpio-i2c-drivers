#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#define DRIVER_NAME "custom_bcm2835_i2c"

/* BMP280 registers */
#define BMP280_REG_CHIPID      0xD0
#define BMP280_REG_RESET       0xE0
#define BMP280_REG_STATUS      0xF3
#define BMP280_REG_CTRL_MEAS   0xF4
#define BMP280_REG_CONFIG      0xF5
#define BMP280_REG_PRESS_MSB   0xF7
#define BMP280_REG_CALIB00     0x88  /* 0x88..0xA1 (24 bytes) */

struct bmp280_calib {
	u16 dig_T1;
	s16 dig_T2;
	s16 dig_T3;
	u16 dig_P1;
	s16 dig_P2;
	s16 dig_P3;
	s16 dig_P4;
	s16 dig_P5;
	s16 dig_P6;
	s16 dig_P7;
	s16 dig_P8;
	s16 dig_P9;
};

struct custom_bmp280 {
	struct i2c_client *client;
	struct bmp280_calib calib;
	struct regmap *regmap; 
	s32 t_fine; /* used between temperature and pressure compensation */
};

/* Compensation algorithms from datasheet (integer) */
/* Returns temperature in centi-degrees Celsius (e.g., 5123 => 51.23 C) */
static s32 bmp280_compensate_T(struct custom_bmp280 *dev, s32 adc_T)
{
	s32 var1, var2, T;
	var1 = ((((adc_T >> 3) - ((s32)dev->calib.dig_T1 << 1))) * ((s32)dev->calib.dig_T2)) >> 11;
	var2 = (((((adc_T >> 4) - ((s32)dev->calib.dig_T1)) * ((adc_T >> 4) - ((s32)dev->calib.dig_T1))) >> 12) *
		((s32)dev->calib.dig_T3)) >> 14;
	dev->t_fine = var1 + var2;
	T = (dev->t_fine * 5 + 128) >> 8;

	return T;
}

static u32 bmp280_compensate_P(struct custom_bmp280 *dev, s32 adc_P)
{
    s32 var1, var2;
    u32 p;

    var1 = (((s32)dev->t_fine) >> 1) - (s32)64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) *
           ((s32)dev->calib.dig_P6);
    var2 = var2 + ((var1 * ((s32)dev->calib.dig_P5)) << 1);
    var2 = (var2 >> 2) + (((s32)dev->calib.dig_P4) << 16);
    var1 = (((dev->calib.dig_P3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) +
            ((((s32)dev->calib.dig_P2) * var1) >> 1)) >> 18;
    var1 = ((((32768 + var1)) * ((s32)dev->calib.dig_P1)) >> 15);
    if (var1 == 0) {
        return 0; /* avoid exception caused by division by zero */
    }
    p = (((u32)(((s32)1048576) - adc_P) - (var2 >> 12))) * 3125;
    if (p < 0x80000000) {
        p = (p << 1) / (u32)var1;
    } else {
        p = (p / (u32)var1) * 2;
    }
    var1 = (((s32)dev->calib.dig_P9) *
           ((s32)(((p >> 3) * (p >> 3)) >> 13))) >> 12;
    var2 = (((s32)(p >> 2)) * ((s32)dev->calib.dig_P8)) >> 13;
    p = (u32)((s32)p + ((var1 + var2 + dev->calib.dig_P7) >> 4));

    return p;
}

static int bmp280_read_regs(struct custom_bmp280 *dev, u8 reg, u8 *buf, int len)
{
    int ret;

    ret = regmap_bulk_read(dev->regmap, reg, buf, len);
    if (ret < 0)
        return ret;

    return 0;
}

static int bmp280_write_reg(struct custom_bmp280 *dev, u8 reg, u8 val)
{
    int ret;
    ret = regmap_write(dev->regmap, reg, val);
    if (ret)
        dev_err(&dev->client->dev, "regmap_write failed at 0x%02x: %d\n", reg, ret);
    return ret;
}

static int bmp280_read_calib(struct custom_bmp280 *dev)
{
    u8 buf[24];
    int ret;

    ret = bmp280_read_regs(dev, BMP280_REG_CALIB00, buf, sizeof(buf));
    if (ret < 0)
        return ret;

    dev->calib.dig_T1 = (u16)((buf[1] << 8) | buf[0]);
    dev->calib.dig_T2 = (s16)((buf[3] << 8) | buf[2]);
    dev->calib.dig_T3 = (s16)((buf[5] << 8) | buf[4]);
    dev->calib.dig_P1 = (u16)((buf[7] << 8) | buf[6]);
    dev->calib.dig_P2 = (s16)((buf[9] << 8) | buf[8]);
    dev->calib.dig_P3 = (s16)((buf[11] << 8) | buf[10]);
    dev->calib.dig_P4 = (s16)((buf[13] << 8) | buf[12]);
    dev->calib.dig_P5 = (s16)((buf[15] << 8) | buf[14]);
    dev->calib.dig_P6 = (s16)((buf[17] << 8) | buf[16]);
    dev->calib.dig_P7 = (s16)((buf[19] << 8) | buf[18]);
    dev->calib.dig_P8 = (s16)((buf[21] << 8) | buf[20]);
    dev->calib.dig_P9 = (s16)((buf[23] << 8) | buf[22]);

    dev_info(&dev->client->dev, "BMP280 calib read\n");
    return 0;
}

static int bmp280_read_raw_temp_press(struct custom_bmp280 *dev, s32 *adc_T, s32 *adc_P)
{
    u8 buf[6];
    int ret;

    ret = bmp280_read_regs(dev, BMP280_REG_PRESS_MSB, buf, sizeof(buf));
    if (ret < 0)
        return ret;

    *adc_P = ((s32)buf[0] << 12) | ((s32)buf[1] << 4) | (buf[2] >> 4);
    *adc_T = ((s32)buf[3] << 12) | ((s32)buf[4] << 4) | (buf[5] >> 4);

    return 0;
}

static int custom_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret;
    unsigned int chip_id;
	struct custom_bmp280 *dev;
    struct regmap_config config = {
        .reg_bits = 8,
        .val_bits = 8,
		.max_register = 0xFF,
    };
	s32 adc_T, adc_P, temp;
	u32 press;

    pr_info("%s: probe called\n", DRIVER_NAME);

	dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->client = client;
	i2c_set_clientdata(client, dev);

    /* regmap initialization over i2c */
	dev->regmap = devm_regmap_init_i2c(client, &config);
    if (IS_ERR(dev->regmap)) {
		dev_err(&client->dev, "regmap init failed\n");
		return PTR_ERR(dev->regmap);
	}

    /* try to read chip id register */
    ret = regmap_read(dev->regmap, BMP280_REG_CHIPID, &chip_id);
    if (ret) {
        pr_err("%s: regmap_read failed: %d\n", DRIVER_NAME, ret);
        return ret;
    }

    pr_info("%s: detected chip id 0x%02x\n", DRIVER_NAME, chip_id);

	msleep(10);

	// ctrl_meas: osrs_t=1, osrs_p=1, mode=normal
	regmap_write(dev->regmap, 0xF4, 0x27);

	msleep(10);

	// config: standby=0.5ms, filter=off
	regmap_write(dev->regmap, 0xF5, 0x00);

	msleep(10);

	/* read calibration */
    ret = bmp280_read_calib(dev);
    if (ret) {
		dev_err(&client->dev, "Failed to read calib data in probe\n");
        return ret;
	}

	msleep(10);

    /* read raw temperature and pressure */
    ret = bmp280_read_raw_temp_press(dev, &adc_T, &adc_P);
    if (ret) {
		dev_err(&client->dev, "Failed to read raw data in probe\n");
        return ret;
	}
	dev_info(&client->dev, "Raw ADC: T=%d, P=%d\n", adc_T, adc_P);

	temp = bmp280_compensate_T(dev, adc_T);
	dev_info(&client->dev, "%s: compensated temp=%d.%02d C",
        DRIVER_NAME,
        temp / 100, temp % 100);

	press = bmp280_compensate_P(dev, adc_P);
	dev_info(&client->dev, "%s: compensated pressure=%u.%02u hPa",
		DRIVER_NAME,
		press / 100, press % 100);

    return 0;
}

static int custom_remove(struct i2c_client *client)
{
    pr_info("Custom I2C client removed\n");
    return 0;
}

static const struct of_device_id custom_of_match[] = {
    { .compatible = "puda,custom_bcm2835_i2c" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, custom_of_match);

static const struct i2c_device_id custom_id[] = {
    { "custom_bcm2835_i2c", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, custom_id);

static struct i2c_driver custom_i2c_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = of_match_ptr(custom_of_match),
    },
    .probe = custom_probe,
    .remove = custom_remove,
    .id_table = custom_id,
};

module_i2c_driver(custom_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Puda");
MODULE_DESCRIPTION("Custom BCM2835 I2C client driver");