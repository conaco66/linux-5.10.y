// SPDX-License-Identifier: GPL-2.0+
/*
 * acm_ms.c -- Composite driver, with ACM and mass storage support
 *
 * Copyright (C) 2008 David Brownell
 * Copyright (C) 2008 Nokia Corporation
 * Author: David Brownell
 * Modified: Klaus Schwarzkopf <schwarzkopf@sensortherm.de>
 *
 * Heavily based on multi.c and cdc2.c
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "u_serial.h"

#define DRIVER_DESC		"Composite Gadget (ACM + MS)"
#define DRIVER_VERSION		"2011/10/10"

/*-------------------------------------------------------------------------*/

/*
 * DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */
#define ACM_MS_VENDOR_NUM	0x1d6b	/* Linux Foundation */
#define ACM_MS_PRODUCT_NUM	0x0106	/* Composite Gadget: ACM + MS*/

#include "f_mass_storage.h"

#include "f_hid.c"  
/*-------------------------------------------------------------------------*/
//add 1
/* hid descriptor for a keyboard */
static struct hidg_func_descriptor my_hid_data = {
    .subclass        = 0, /* No subclass */
    .protocol        = 1, /* Keyboard */
    .report_length        = 8,
    .report_desc_length    = 63,
    .report_desc        = {
        0x05, 0x01,    /* USAGE_PAGE (Generic Desktop)     */
        0x09, 0x06,    /* USAGE (Keyboard) */
        0xa1, 0x01,    /* COLLECTION (Application) */
        0x05, 0x07,    /* USAGE_PAGE (Keyboard) */
        0x19, 0xe0,    /* USAGE_MINIMUM (Keyboard LeftControl) */
        0x29, 0xe7,    /* USAGE_MAXIMUM (Keyboard Right GUI) */
        0x15, 0x00,    /* LOGICAL_MINIMUM (0) */
        0x25, 0x01,    /* LOGICAL_MAXIMUM (1) */
        0x75, 0x01,    /* REPORT_SIZE (1) */
        0x95, 0x08,    /* REPORT_COUNT (8) */
        0x81, 0x02,    /* INPUT (Data,Var,Abs) */
        0x95, 0x01,    /* REPORT_COUNT (1) */
        0x75, 0x08,    /* REPORT_SIZE (8) */
        0x81, 0x03,    /* INPUT (Cnst,Var,Abs) */
        0x95, 0x05,    /* REPORT_COUNT (5) */
        0x75, 0x01,    /* REPORT_SIZE (1) */
        0x05, 0x08,    /* USAGE_PAGE (LEDs) */
        0x19, 0x01,    /* USAGE_MINIMUM (Num Lock) */
        0x29, 0x05,    /* USAGE_MAXIMUM (Kana) */
        0x91, 0x02,    /* OUTPUT (Data,Var,Abs) */
        0x95, 0x01,    /* REPORT_COUNT (1) */
        0x75, 0x03,    /* REPORT_SIZE (3) */
        0x91, 0x03,    /* OUTPUT (Cnst,Var,Abs) */
        0x95, 0x06,    /* REPORT_COUNT (6) */
        0x75, 0x08,    /* REPORT_SIZE (8) */
        0x15, 0x00,    /* LOGICAL_MINIMUM (0) */
        0x25, 0x65,    /* LOGICAL_MAXIMUM (101) */
        0x05, 0x07,    /* USAGE_PAGE (Keyboard) */
        0x19, 0x00,    /* USAGE_MINIMUM (Reserved) */
        0x29, 0x65,    /* USAGE_MAXIMUM (Keyboard Application) */
        0x81, 0x00,    /* INPUT (Data,Ary,Abs) */
        0xc0        /* END_COLLECTION */
    }
};

//add 2
static struct platform_device my_hid = {
    .name = "hidg",
    .id            = 0,
    .num_resources = 0,
    .resource    = 0,
    .dev.platform_data = &my_hid_data,
};

//add 3
struct hidg_func_node {
	struct usb_function_instance *fi;
	struct usb_function *f;
	struct list_head node;
	struct hidg_func_descriptor *func;
};
static LIST_HEAD(hidg_func_list);
//end 3

/*-------------------------------------------------------------------------*/
USB_GADGET_COMPOSITE_OPTIONS();

static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	/* .bcdUSB = DYNAMIC */

	.bDeviceClass =		USB_CLASS_MISC /* 0xEF */,
	.bDeviceSubClass =	2,
	.bDeviceProtocol =	1,

	/* .bMaxPacketSize0 = f(hardware) */

	/* Vendor and product id can be overridden by module parameters.  */
	//add 4 fix id
	.idVendor =		cpu_to_le16(FSG_VENDOR_ID),
	.idProduct =		cpu_to_le16(FSG_PRODUCT_ID),
	/* .bcdDevice = f(hardware) */
	/* .iManufacturer = DYNAMIC */
	/* .iProduct = DYNAMIC */
	/* NO SERIAL NUMBER */
	/*.bNumConfigurations =	DYNAMIC*/
};

static const struct usb_descriptor_header *otg_desc[2];

/* string IDs are assigned dynamically */
static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "",
	[USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
	[USB_GADGET_SERIAL_IDX].s = "",
	{  } /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};

/****************************** Configurations ******************************/

static struct fsg_module_parameters fsg_mod_data = { .stall = 1 };
#ifdef CONFIG_USB_GADGET_DEBUG_FILES

static unsigned int fsg_num_buffers = CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS;

#else

/*
 * Number of buffers we will use.
 * 2 is usually enough for good buffering pipeline
 */
#define fsg_num_buffers	CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS

#endif /* CONFIG_USB_GADGET_DEBUG_FILES */

FSG_MODULE_PARAMETERS(/* no prefix */, fsg_mod_data);

/*-------------------------------------------------------------------------*/
static struct usb_function *f_acm;
static struct usb_function_instance *f_acm_inst;

static struct usb_function_instance *fi_msg;
static struct usb_function *f_msg;

/*
 * We _always_ have both ACM and mass storage functions.
 */
static int acm_ms_do_config(struct usb_configuration *c)
{
	int	status;
//add 4
	struct hidg_func_node *e, *n;
	
	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}
//add 5
	list_for_each_entry(e, &hidg_func_list, node) {
		e->f = usb_get_function(e->fi);
		if (IS_ERR(e->f)) {
			status = PTR_ERR(e->f);
			goto put;
		}
		status = usb_add_function(c, e->f);
		if (status < 0) {
			usb_put_function(e->f);
			goto put;
		}
	}
	
//	f_acm = usb_get_function(f_acm_inst);
//	if (IS_ERR(f_acm))
//		return PTR_ERR(f_acm);

	f_msg = usb_get_function(fi_msg);
	if (IS_ERR(f_msg)) {
		status = PTR_ERR(f_msg);
		goto put_acm;
	}

//	status = usb_add_function(c, f_acm);
//	if (status < 0)
//		goto put_msg;

	status = usb_add_function(c, f_msg);
	if (status)
		goto remove_acm;

	return 0;
remove_acm:
//	usb_remove_function(c, f_acm);
put_msg:
	usb_put_function(f_msg);
put_acm:
	usb_put_function(f_acm);
	return status;
//add 6
put:
	list_for_each_entry(n, &hidg_func_list, node) {
		if (n == e)
			break;
		usb_remove_function(c, n->f);
		usb_put_function(n->f);
	}
	return status;
}

static struct usb_configuration acm_ms_config_driver = {
	.label			= DRIVER_DESC,
	.bConfigurationValue	= 1,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
};

/*-------------------------------------------------------------------------*/

static int acm_ms_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget	*gadget = cdev->gadget;
	struct fsg_opts		*opts;
	struct fsg_config	config;
	int			status;
//add 7
	struct list_head *tmp;
	struct hidg_func_node *n, *m;
	struct f_hid_opts *hid_opts;
	int funcs = 0;
	list_for_each(tmp, &hidg_func_list)
	funcs++;
	if (!funcs)
		return -ENODEV;
//7
//add 8
	list_for_each_entry(n, &hidg_func_list, node) {
		n->fi = usb_get_function_instance("hid");
		if (IS_ERR(n->fi)) {
			status = PTR_ERR(n->fi);
			goto put;
		}
		hid_opts = container_of(n->fi, struct f_hid_opts, func_inst);
		hid_opts->subclass = n->func->subclass;
		hid_opts->protocol = n->func->protocol;
		hid_opts->report_length = n->func->report_length;
		hid_opts->report_desc_length = n->func->report_desc_length;
		hid_opts->report_desc = n->func->report_desc;
	}
//8
	
//	f_acm_inst = usb_get_function_instance("acm");
//	if (IS_ERR(f_acm_inst))
//		return PTR_ERR(f_acm_inst);

	fi_msg = usb_get_function_instance("mass_storage");
	if (IS_ERR(fi_msg)) {
		status = PTR_ERR(fi_msg);
		goto fail_get_msg;
	}

	/* set up mass storage function */
	fsg_config_from_params(&config, &fsg_mod_data, fsg_num_buffers);
	opts = fsg_opts_from_func_inst(fi_msg);

	opts->no_configfs = true;
	status = fsg_common_set_num_buffers(opts->common, fsg_num_buffers);
	if (status)
		goto fail;

	status = fsg_common_set_cdev(opts->common, cdev, config.can_stall);
	if (status)
		goto fail_set_cdev;

	fsg_common_set_sysfs(opts->common, true);
	status = fsg_common_create_luns(opts->common, &config);
	if (status)
		goto fail_set_cdev;

	fsg_common_set_inquiry_string(opts->common, config.vendor_name,
				      config.product_name);
	/*
	 * Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */
	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		goto fail_string_ids;
	device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;

	if (gadget_is_otg(gadget) && !otg_desc[0]) {
		struct usb_descriptor_header *usb_desc;

		usb_desc = usb_otg_descriptor_alloc(gadget);
		if (!usb_desc) {
			status = -ENOMEM;
			goto fail_string_ids;
		}
		usb_otg_descriptor_init(gadget, usb_desc);
		otg_desc[0] = usb_desc;
		otg_desc[1] = NULL;
	}

	/* register our configuration */
	status = usb_add_config(cdev, &acm_ms_config_driver, acm_ms_do_config);
	if (status < 0)
		goto fail_otg_desc;

	usb_composite_overwrite_options(cdev, &coverwrite);
	dev_info(&gadget->dev, "%s, version: " DRIVER_VERSION "\n",
			DRIVER_DESC);
	return 0;

	/* error recovery */
fail_otg_desc:
	kfree(otg_desc[0]);
	otg_desc[0] = NULL;
fail_string_ids:
	fsg_common_remove_luns(opts->common);
fail_set_cdev:
	fsg_common_free_buffers(opts->common);
fail:
	usb_put_function_instance(fi_msg);
fail_get_msg:
// 	usb_put_function_instance(f_acm_inst);
//add 8.5
put:
	list_for_each_entry(m, &hidg_func_list, node) {
		if (m == n)
			break;
		usb_put_function_instance(m->fi);
	}
	return status;
}

static int acm_ms_unbind(struct usb_composite_dev *cdev)
{
//	usb_put_function(f_msg);
//	usb_put_function_instance(fi_msg);
	usb_put_function(f_acm);
	usb_put_function_instance(f_acm_inst);
	kfree(otg_desc[0]);
	otg_desc[0] = NULL;

	return 0;
}
//add 9
static int hidg_plat_driver_probe(struct platform_device *pdev)
{
	struct hidg_func_descriptor *func = dev_get_platdata(&pdev->dev);
	struct hidg_func_node *entry;

	if (!func) {
		dev_err(&pdev->dev, "Platform data missing\n");
		return -ENODEV;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->func = func;
	list_add_tail(&entry->node, &hidg_func_list);

	return 0;
}
static int hidg_plat_driver_remove(struct platform_device *pdev)
{
	struct hidg_func_node *e, *n;

	list_for_each_entry_safe(e, n, &hidg_func_list, node) {
		list_del(&e->node);
		kfree(e);
	}

	return 0;
}
// end 9

static struct usb_composite_driver acm_ms_driver = {
	.name		= "g_acm_ms",
	.dev		= &device_desc,
	.max_speed	= USB_SPEED_SUPER,
	.strings	= dev_strings,
	.bind		= acm_ms_bind,
	.unbind		= acm_ms_unbind,
};
// add 10
static struct platform_driver hidg_plat_driver = {
	.remove		= hidg_plat_driver_remove,
	.driver		= {
	.name	= "hidg",
	},
};

//module_usb_composite_driver(acm_ms_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Klaus Schwarzkopf <schwarzkopf@sensortherm.de>");
MODULE_LICENSE("GPL v2");

//add 11
static int __init init(void)
{
	int status; 
	status = platform_device_register(&my_hid);
	if (status < 0) {
		printk("keyboard device register failed!\n");
		return status;
	}	
	status = platform_driver_probe(&hidg_plat_driver,
				hidg_plat_driver_probe);
	if (status < 0)
		{
		return status;
    }
	status = usb_composite_probe(&acm_ms_driver);
	return status;
}
module_init(init);

static void __exit cleanup(void)
{
	usb_composite_unregister(&acm_ms_driver);
}
module_exit(cleanup);
