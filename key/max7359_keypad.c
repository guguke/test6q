/*
 * max7359_keypad.c - MAX7359 Key Switch Controller Driver
 *
 * Copyright (C) 2009 Samsung Electronics
 * Kim Kyuwon <q1.kim@samsung.com>
 *
 * Based on pxa27x_keypad.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Datasheet: http://www.maxim-ic.com/quick_view2.cfm/qv_pk/5456
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>

#define MAX7359_MAX_KEY_ROWS	8
#define MAX7359_MAX_KEY_COLS	8
#define MAX7359_MAX_KEY_NUM	(MAX7359_MAX_KEY_ROWS * MAX7359_MAX_KEY_COLS)
#define MAX7359_ROW_SHIFT	3

/*
 * MAX7359 registers
 */
#define MAX7359_REG_KEYFIFO	0x00
#define MAX7359_REG_CONFIG	0x01
#define MAX7359_REG_DEBOUNCE	0x02
#define MAX7359_REG_INTERRUPT	0x03
#define MAX7359_REG_PORTS	0x04
#define MAX7359_REG_KEYREP	0x05
#define MAX7359_REG_SLEEP	0x06

/*
 * Configuration register bits
 */
#define MAX7359_CFG_SLEEP	(1 << 7)
#define MAX7359_CFG_INTERRUPT	(1 << 5)
#define MAX7359_CFG_KEY_RELEASE	(1 << 3)
#define MAX7359_CFG_WAKEUP	(1 << 1)
#define MAX7359_CFG_TIMEOUT	(1 << 0)

/*
 * Autosleep register values (ms)
 */
#define MAX7359_AUTOSLEEP_8192	0x01
#define MAX7359_AUTOSLEEP_4096	0x02
#define MAX7359_AUTOSLEEP_2048	0x03
#define MAX7359_AUTOSLEEP_1024	0x04
#define MAX7359_AUTOSLEEP_512	0x05
#define MAX7359_AUTOSLEEP_256	0x06

struct max7359_keypad {
	/* matrix key code map */
	unsigned short keycodes[MAX7359_MAX_KEY_NUM];

	struct input_dev *input_dev;
	struct i2c_client *client;
};
static uint32_t my7359_keymap[]={
	KEY(0,0,KEY_1), KEY(0,1,KEY_9), KEY(0,2,KEY_G), KEY(0,3,KEY_P), KEY(0,4,KEY_X), KEY(0,5,KEY_F6), KEY(0,5,KEY_F14), KEY(0,7,KEY_ENTER),
	KEY(1,0,KEY_2), KEY(1,1,KEY_0), KEY(1,2,KEY_H), KEY(1,3,KEY_Q), KEY(1,4,KEY_Y), KEY(1,5,KEY_F7), KEY(1,5,KEY_F15), KEY(1,7,KEY_ENTER),
	KEY(2,0,KEY_3), KEY(2,1,KEY_A), KEY(2,2,KEY_I), KEY(2,3,KEY_R), KEY(2,4,KEY_Z), KEY(2,5,KEY_F8), KEY(2,5,KEY_F16), KEY(2,7,KEY_ENTER),
	KEY(3,0,KEY_4), KEY(3,1,KEY_B), KEY(3,2,KEY_J), KEY(3,3,KEY_S), KEY(3,4,KEY_F1), KEY(3,5,KEY_F9), KEY(3,5,KEY_ENTER), KEY(3,7,KEY_ENTER),
	KEY(4,0,KEY_5), KEY(4,1,KEY_C), KEY(4,2,KEY_K), KEY(4,3,KEY_T), KEY(4,4,KEY_F2), KEY(4,5,KEY_F10), KEY(4,5,KEY_ENTER), KEY(4,7,KEY_ENTER),
	KEY(5,0,KEY_6), KEY(5,1,KEY_D), KEY(5,2,KEY_M), KEY(5,3,KEY_U), KEY(5,4,KEY_F3), KEY(5,5,KEY_F11), KEY(5,5,KEY_ENTER), KEY(5,7,KEY_ENTER),
	KEY(6,0,KEY_7), KEY(6,1,KEY_E), KEY(6,2,KEY_N), KEY(6,3,KEY_V), KEY(6,4,KEY_F4), KEY(6,5,KEY_F12), KEY(6,5,KEY_ENTER), KEY(6,7,KEY_ENTER),
	KEY(7,0,KEY_8), KEY(7,1,KEY_F), KEY(7,2,KEY_O), KEY(7,3,KEY_W), KEY(7,4,KEY_F5), KEY(7,5,KEY_F13), KEY(7,5,KEY_ENTER), KEY(7,7,KEY_ENTER)
};
static struct matrix_keymap_data my7359_data ={
	.keymap=my7359_keymap,
	.keymap_size=64,
};

static int max7359_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int ret = i2c_smbus_write_byte_data(client, reg, val);

	if (ret < 0)
		dev_err(&client->dev, "%s: reg 0x%x, val 0x%x, err %d\n",
			__func__, reg, val, ret);
	return ret;
}

static int max7359_read_reg(struct i2c_client *client, int reg)
{
	int ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0)
		dev_err(&client->dev, "%s: reg 0x%x, err %d\n",
			__func__, reg, ret);
	return ret;
}

static void max7359_build_keycode(struct max7359_keypad *keypad,
				const struct matrix_keymap_data *keymap_data1)
{
	struct input_dev *input_dev = keypad->input_dev;
	int i;
	struct matrix_keymap_data *keymap_data=&my7359_data;

	for (i = 0; i < keymap_data->keymap_size; i++) {
		unsigned int key = keymap_data->keymap[i];
		unsigned int row = KEY_ROW(key);
		unsigned int col = KEY_COL(key);
		unsigned int scancode = MATRIX_SCAN_CODE(row, col,
						MAX7359_ROW_SHIFT);
		unsigned short keycode = KEY_VAL(key);

		keypad->keycodes[scancode] = keycode;
		__set_bit(keycode, input_dev->keybit);
	}
	__clear_bit(KEY_RESERVED, input_dev->keybit);
}

/* runs in an IRQ thread -- can (and will!) sleep */
static irqreturn_t max7359_interrupt(int irq, void *dev_id)
{
	struct max7359_keypad *keypad = dev_id;
	struct input_dev *input_dev = keypad->input_dev;
	int val, row, col, release, code;

	val = max7359_read_reg(keypad->client, MAX7359_REG_KEYFIFO);
	row = val & 0x7;
	col = (val >> 3) & 0x7;
	release = val & 0x40;

	code = MATRIX_SCAN_CODE(row, col, MAX7359_ROW_SHIFT);
	//if(code!=63&&code!=62)
		printk(KERN_DEBUG"%s   code:%d   val:0x%02x\n",__FUNCTION__,code,val);

	dev_dbg(&keypad->client->dev,
		"key[%d:%d] %s\n", row, col, release ? "release" : "press");

	input_event(input_dev, EV_MSC, MSC_SCAN, code);
	input_report_key(input_dev, keypad->keycodes[code], !release);
	input_sync(input_dev);

	return IRQ_HANDLED;
}

/*
 * Let MAX7359 fall into a deep sleep:
 * If no keys are pressed, enter sleep mode for 8192 ms. And if any
 * key is pressed, the MAX7359 returns to normal operating mode.
 */
static inline void max7359_fall_deepsleep(struct i2c_client *client)
{
	max7359_write_reg(client, MAX7359_REG_SLEEP, MAX7359_AUTOSLEEP_8192);
}

/*
 * Let MAX7359 take a catnap:
 * Autosleep just for 256 ms.
 */
static inline void max7359_take_catnap(struct i2c_client *client)
{
	max7359_write_reg(client, MAX7359_REG_SLEEP, MAX7359_AUTOSLEEP_256);
}

static int max7359_open(struct input_dev *dev)
{
	struct max7359_keypad *keypad = input_get_drvdata(dev);

	max7359_take_catnap(keypad->client);

	return 0;
}

static void max7359_close(struct input_dev *dev)
{
	struct max7359_keypad *keypad = input_get_drvdata(dev);

	max7359_fall_deepsleep(keypad->client);
}

static void max7359_initialize(struct i2c_client *client)
{
	max7359_write_reg(client, MAX7359_REG_CONFIG,
		//MAX7359_CFG_INTERRUPT | /* Irq clears after host read */
		MAX7359_CFG_KEY_RELEASE | /* Key release enable */
		MAX7359_CFG_WAKEUP); /* Key press wakeup enable */

	/* Full key-scan functionality */
	max7359_write_reg(client, MAX7359_REG_DEBOUNCE, 0x1F);

	/* nINT asserts every debounce cycles */
	max7359_write_reg(client, MAX7359_REG_INTERRUPT, 0x01);

	max7359_fall_deepsleep(client);
}

static int __devinit max7359_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	const struct matrix_keymap_data *keymap_data = client->dev.platform_data;
	struct max7359_keypad *keypad;
	struct input_dev *input_dev;
	int ret;
	int error;

	if (!client->irq) {
		dev_err(&client->dev, "The irq number should not be zero\n");
		return -EINVAL;
	}
	printk(KERN_ERR"%s       1\n",__FUNCTION__);

	/* Detect MAX7359: The initial Keys FIFO value is '0x3F' */
	ret = max7359_read_reg(client, MAX7359_REG_KEYFIFO);
	if (ret < 0) {
		dev_err(&client->dev, "failed to detect device\n");
		return -ENODEV;
	}
	printk(KERN_ERR"%s       11\n",__FUNCTION__);

	dev_dbg(&client->dev, "keys FIFO is 0x%02x\n", ret);

	keypad = kzalloc(sizeof(struct max7359_keypad), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!keypad || !input_dev) {
		dev_err(&client->dev, "failed to allocate memory\n");
		error = -ENOMEM;
		goto failed_free_mem;
	}
	printk(KERN_ERR"%s       12\n",__FUNCTION__);

	keypad->client = client;
	keypad->input_dev = input_dev;

	input_dev->name = client->name;
	input_dev->id.bustype = BUS_I2C;
	input_dev->open = max7359_open;
	input_dev->close = max7359_close;
	input_dev->dev.parent = &client->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
	input_dev->keycodesize = sizeof(keypad->keycodes[0]);
	input_dev->keycodemax = ARRAY_SIZE(keypad->keycodes);
	input_dev->keycode = keypad->keycodes;

	input_set_capability(input_dev, EV_MSC, MSC_SCAN);
	input_set_drvdata(input_dev, keypad);
	printk(KERN_ERR"%s       13\n",__FUNCTION__);

	max7359_build_keycode(keypad, keymap_data);

	printk(KERN_ERR"%s       irq.num:%d\n",__FUNCTION__,client->irq);
	error = request_threaded_irq(client->irq, NULL, max7359_interrupt,
				     IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				     client->name, keypad);
	if (error) {
		dev_err(&client->dev, "failed to register interrupt\n");
		goto failed_free_mem;
	}

	/* Register the input device */
	error = input_register_device(input_dev);
	if (error) {
		dev_err(&client->dev, "failed to register input device\n");
		goto failed_free_irq;
	}
	printk(KERN_ERR"%s       2\n",__FUNCTION__);

	/* Initialize MAX7359 */
	max7359_initialize(client);

	printk(KERN_ERR"%s       3\n",__FUNCTION__);
	i2c_set_clientdata(client, keypad);
	device_init_wakeup(&client->dev, 1);

	printk(KERN_ERR"%s       4\n",__FUNCTION__);
	return 0;

failed_free_irq:
	free_irq(client->irq, keypad);
failed_free_mem:
	input_free_device(input_dev);
	kfree(keypad);
	return error;
}

static int __devexit max7359_remove(struct i2c_client *client)
{
	struct max7359_keypad *keypad = i2c_get_clientdata(client);

	free_irq(client->irq, keypad);
	input_unregister_device(keypad->input_dev);
	kfree(keypad);

	return 0;
}

#ifdef CONFIG_PM
static int max7359_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	max7359_fall_deepsleep(client);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);

	return 0;
}

static int max7359_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);

	/* Restore the default setting */
	max7359_take_catnap(client);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(max7359_pm, max7359_suspend, max7359_resume);

static const struct i2c_device_id max7359_ids[] = {
	{ "max7359", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max7359_ids);

static struct i2c_driver max7359_i2c_driver = {
	.driver = {
		.name = "max7359",
		.pm   = &max7359_pm,
	},
	.probe		= max7359_probe,
	.remove		= __devexit_p(max7359_remove),
	.id_table	= max7359_ids,
};

static int __init max7359_init(void)
{
	return i2c_add_driver(&max7359_i2c_driver);
}
module_init(max7359_init);

static void __exit max7359_exit(void)
{
	i2c_del_driver(&max7359_i2c_driver);
}
module_exit(max7359_exit);

MODULE_AUTHOR("Kim Kyuwon <q1.kim@samsung.com>");
MODULE_DESCRIPTION("MAX7359 Key Switch Controller Driver");
MODULE_LICENSE("GPL v2");
