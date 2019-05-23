// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2019 - All Rights Reserved
 * Authors: Arnaud Pouliquen <arnaud.pouliquen@st.com> for STMicroelectronics.
 *          Fabien Dessenne <fabien.dessenne@st.com> for STMicroelectronics.
 */

#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#define MAX_TTY_RPMSG		32

static DEFINE_IDR(tty_idr);	/* tty instance id */
static DEFINE_MUTEX(idr_lock);	/* protects tty_idr */

static struct tty_driver *rpmsg_tty_driver;

enum rpmsg_tty_type_t {
	RPMSG_DATA,
	RPMSG_CTRL,
	NUM_RPMSG_TTY_TYPE
};

enum rpmsg_tty_ctrl_t {
	DATA_TERM_READY,	/* ready to accept data */
	NUM_RPMSG_TTY_CTRL_TYPE
};

struct rpmsg_tty_payload {
	u8 cmd;
	u8 data[0];
};

struct rpmsg_tty_ctrl {
	u8 ctrl;
	u8 values[0];
};

struct rpmsg_tty_port {
	struct tty_port		port;	 /* TTY port data */
	int			id;	 /* TTY rpmsg index */
	int			cts;	 /* remote reception status */
	struct rpmsg_device	*rpdev;	 /* rpmsg device */
};

typedef void (*rpmsg_tty_rx_cb_t)(struct rpmsg_device *, void *, int, void *,
				  u32);

static void rpmsg_tty_data_handler(struct rpmsg_device *rpdev, void *data,
				   int len, void *priv, u32 src)
{
	struct rpmsg_tty_port *cport = dev_get_drvdata(&rpdev->dev);
	int copied;

	dev_dbg(&rpdev->dev, "msg(<- src 0x%x) len %d\n", src, len);

	if (!len)
		return;

	copied = tty_insert_flip_string_fixed_flag(&cport->port, data,
						   TTY_NORMAL, len);
	if (copied != len)
		dev_dbg(&rpdev->dev, "trunc buffer: available space is %d\n",
			copied);
	tty_flip_buffer_push(&cport->port);
}

static void rpmsg_tty_ctrl_handler(struct rpmsg_device *rpdev, void *data,
				   int len, void *priv, u32 src)
{
	struct rpmsg_tty_port *cport = dev_get_drvdata(&rpdev->dev);
	struct rpmsg_tty_ctrl *ctrl = data;

	dev_dbg(&rpdev->dev, "%s: ctrl received %d\n", __func__, ctrl->ctrl);
	print_hex_dump_debug(__func__, DUMP_PREFIX_NONE, 16, 1, data, len,
			     true);

	if (len <= sizeof(*ctrl)) {
		dev_err(&rpdev->dev, "%s: ctrl message invalid\n", __func__);
		return;
	}

	if (ctrl->ctrl == DATA_TERM_READY) {
		/* Update the CTS according to remote RTS */
		if (!ctrl->values[0]) {
			cport->cts = 0;
		} else {
			cport->cts = 1;
			tty_port_tty_wakeup(&cport->port);
		}
	} else {
		dev_err(&rpdev->dev, "unknown control ID %d\n", ctrl->ctrl);
	}
}

static const rpmsg_tty_rx_cb_t rpmsg_tty_handler[] = {
	[RPMSG_DATA] = rpmsg_tty_data_handler,
	[RPMSG_CTRL] = rpmsg_tty_ctrl_handler,
};

static int rpmsg_tty_cb(struct rpmsg_device *rpdev, void *data, int len,
			void *priv, u32 src)
{
	struct rpmsg_tty_payload  *rbuf = data;

	if (len <= sizeof(*rbuf) || rbuf->cmd >= NUM_RPMSG_TTY_TYPE) {
		dev_err(&rpdev->dev, "Invalid message: size %d, type %d\n",
			len, rbuf->cmd);
		return -EINVAL;
	}

	rpmsg_tty_handler[rbuf->cmd](rpdev, &rbuf->data,
				     len - sizeof(rbuf->cmd), priv, src);

	return 0;
}

static int rpmsg_tty_write_control(struct tty_struct *tty, u8 ctrl, u8 *values,
				   unsigned int n_value)
{
	struct rpmsg_tty_port *cport = tty->driver_data;
	struct rpmsg_tty_payload *msg;
	struct rpmsg_tty_ctrl *m_ctrl;
	struct rpmsg_device *rpdev;
	unsigned int msg_size;
	int ret;

	rpdev = cport->rpdev;

	msg_size = sizeof(*msg) + sizeof(*m_ctrl) + n_value;
	msg = kzalloc(msg_size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->cmd = RPMSG_CTRL;
	m_ctrl =  (struct rpmsg_tty_ctrl *)&msg->data[0];
	m_ctrl->ctrl = DATA_TERM_READY;
	memcpy(m_ctrl->values, values, n_value);

	ret = rpmsg_trysend(rpdev->ept, msg, msg_size);
	if (ret < 0) {
		dev_dbg(tty->dev, "cannot send control (%d)\n", ret);
		ret = 0;
	}
	kfree(msg);

	return ret;
};

static void rpmsg_tty_throttle(struct tty_struct *tty)
{
	u8 rts = 0;

	/* Disable remote transmission */
	rpmsg_tty_write_control(tty, DATA_TERM_READY, &rts, 1);
};

static void rpmsg_tty_unthrottle(struct tty_struct *tty)
{
	u8 rts = 1;

	/* Enable remote transmission */
	rpmsg_tty_write_control(tty, DATA_TERM_READY, &rts, 1);
};

static int rpmsg_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct rpmsg_tty_port *cport = idr_find(&tty_idr, tty->index);

	if (!cport) {
		dev_err(tty->dev, "cannot get cport\n");
		return -ENODEV;
	}

	tty->driver_data = cport;
	return tty_port_install(&cport->port, driver, tty);
}

static int rpmsg_tty_open(struct tty_struct *tty, struct file *filp)
{
	return tty_port_open(tty->port, tty, filp);
}

static void rpmsg_tty_close(struct tty_struct *tty, struct file *filp)
{
	return tty_port_close(tty->port, tty, filp);
}

static int rpmsg_tty_write(struct tty_struct *tty, const u8 *buf, int len)
{
	struct rpmsg_tty_port *cport = tty->driver_data;
	struct rpmsg_device *rpdev;
	int msg_max_size, ret = 0;
	unsigned int msg_size;
	int cmd_sz = sizeof(struct rpmsg_tty_payload);
	u8 *tmpbuf;

	/* If cts not set, the message is not sent*/
	if (!cport->cts)
		return 0;

	rpdev = cport->rpdev;

	dev_dbg(&rpdev->dev, "%s: send msg from tty->index = %d, len = %d\n",
		__func__, tty->index, len);

	msg_max_size = rpmsg_get_buf_payload_size(rpdev->ept);
	if (msg_max_size < 0)
		return msg_max_size;

	msg_size = min(len + cmd_sz, msg_max_size);
	tmpbuf = kzalloc(msg_size, GFP_KERNEL);
	if (!tmpbuf)
		return -ENOMEM;

	tmpbuf[0] = RPMSG_DATA;
	memcpy(&tmpbuf[cmd_sz], buf, msg_size - cmd_sz);

	/*
	 * Try to send the message to remote processor, if failed return 0 as
	 * no data sent
	 */
	ret = rpmsg_trysend(rpdev->ept, tmpbuf, msg_size);
	kfree(tmpbuf);
	if (ret) {
		dev_dbg(&rpdev->dev, "rpmsg_send failed: %d\n", ret);
		return 0;
	}

	return msg_size - cmd_sz;
}

static int rpmsg_tty_write_room(struct tty_struct *tty)
{
	struct rpmsg_tty_port *cport = tty->driver_data;
	int space = 0;

	/*
	 * Report the space in the rpmsg buffer, first byte is reserved to
	 * define the buffer type.
	 */
	if (cport->cts) {
		space = rpmsg_get_buf_payload_size(cport->rpdev->ept);
		space -= sizeof(struct rpmsg_tty_payload);
	}

	return space;
}

static const struct tty_operations rpmsg_tty_ops = {
	.install	= rpmsg_tty_install,
	.open		= rpmsg_tty_open,
	.close		= rpmsg_tty_close,
	.write		= rpmsg_tty_write,
	.write_room	= rpmsg_tty_write_room,
	.throttle	= rpmsg_tty_throttle,
	.unthrottle	= rpmsg_tty_unthrottle,
};

static struct rpmsg_tty_port *rpmsg_tty_alloc_cport(void)
{
	struct rpmsg_tty_port *cport;

	cport = kzalloc(sizeof(*cport), GFP_KERNEL);
	if (!cport)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&idr_lock);
	cport->id = idr_alloc(&tty_idr, cport, 0, MAX_TTY_RPMSG, GFP_KERNEL);
	mutex_unlock(&idr_lock);

	if (cport->id < 0) {
		kfree(cport);
		return ERR_PTR(-ENOSPC);
	}

	return cport;
}

static void rpmsg_tty_release_cport(struct rpmsg_tty_port *cport)
{
	mutex_lock(&idr_lock);
	idr_remove(&tty_idr, cport->id);
	mutex_unlock(&idr_lock);

	kfree(cport);
}

static int rpmsg_tty_port_activate(struct tty_port *p, struct tty_struct *tty)
{
	p->low_latency = (p->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

	/* Allocate the buffer we use for writing data */
	return tty_port_alloc_xmit_buf(p);
}

static void rpmsg_tty_port_shutdown(struct tty_port *p)
{
	/* Free the write buffer */
	tty_port_free_xmit_buf(p);
}

static void rpmsg_tty_dtr_rts(struct tty_port *port, int raise)
{
	struct rpmsg_tty_port *cport =
				container_of(port, struct rpmsg_tty_port, port);

	pr_debug("%s: dtr_rts state %d\n", __func__, raise);

	cport->cts = raise;

	if (raise)
		rpmsg_tty_unthrottle(port->tty);
	else
		rpmsg_tty_throttle(port->tty);
}

static const struct tty_port_operations rpmsg_tty_port_ops = {
	.activate = rpmsg_tty_port_activate,
	.shutdown = rpmsg_tty_port_shutdown,
	.dtr_rts  = rpmsg_tty_dtr_rts,
};

static int rpmsg_tty_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_tty_port *cport;
	struct device *dev = &rpdev->dev;
	struct device *tty_dev;
	int ret;

	cport = rpmsg_tty_alloc_cport();
	if (IS_ERR(cport)) {
		dev_err(dev, "failed to alloc tty port\n");
		return PTR_ERR(cport);
	}

	tty_port_init(&cport->port);
	cport->port.ops = &rpmsg_tty_port_ops;

	tty_dev = tty_port_register_device(&cport->port, rpmsg_tty_driver,
					   cport->id, dev);
	if (IS_ERR(tty_dev)) {
		dev_err(dev, "failed to register tty port\n");
		ret = PTR_ERR(tty_dev);
		goto  err_destroy;
	}

	cport->rpdev = rpdev;

	dev_set_drvdata(dev, cport);

	dev_dbg(dev, "new channel: 0x%x -> 0x%x : ttyRPMSG%d\n",
		rpdev->src, rpdev->dst, cport->id);

	return 0;

err_destroy:
	tty_port_destroy(&cport->port);
	rpmsg_tty_release_cport(cport);

	return ret;
}

static void rpmsg_tty_remove(struct rpmsg_device *rpdev)
{
	struct rpmsg_tty_port *cport = dev_get_drvdata(&rpdev->dev);

	dev_dbg(&rpdev->dev, "removing rpmsg tty device %d\n", cport->id);

	/* User hang up to release the tty */
	if (tty_port_initialized(&cport->port))
		tty_port_tty_hangup(&cport->port, false);

	tty_unregister_device(rpmsg_tty_driver, cport->id);

	tty_port_destroy(&cport->port);
	rpmsg_tty_release_cport(cport);
}

static struct rpmsg_device_id rpmsg_driver_tty_id_table[] = {
	{ .name	= "rpmsg-tty-channel" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_driver_tty_id_table);

static struct rpmsg_driver rpmsg_tty_rpmsg_drv = {
	.drv.name	= KBUILD_MODNAME,
	.id_table	= rpmsg_driver_tty_id_table,
	.probe		= rpmsg_tty_probe,
	.callback	= rpmsg_tty_cb,
	.remove		= rpmsg_tty_remove,
};

static int __init rpmsg_tty_init(void)
{
	int err;

	rpmsg_tty_driver = tty_alloc_driver(MAX_TTY_RPMSG, TTY_DRIVER_REAL_RAW |
					    TTY_DRIVER_DYNAMIC_DEV);
	if (IS_ERR(rpmsg_tty_driver))
		return PTR_ERR(rpmsg_tty_driver);

	rpmsg_tty_driver->driver_name = "rpmsg_tty";
	rpmsg_tty_driver->name = "ttyRPMSG";
	rpmsg_tty_driver->major = 0;
	rpmsg_tty_driver->type = TTY_DRIVER_TYPE_CONSOLE;

	/* Disable unused mode by default */
	rpmsg_tty_driver->init_termios = tty_std_termios;
	rpmsg_tty_driver->init_termios.c_lflag &= ~(ECHO | ICANON);
	rpmsg_tty_driver->init_termios.c_oflag &= ~(OPOST | ONLCR);

	tty_set_operations(rpmsg_tty_driver, &rpmsg_tty_ops);

	err = tty_register_driver(rpmsg_tty_driver);
	if (err < 0) {
		pr_err("Couldn't install rpmsg tty driver: err %d\n", err);
		goto error_put;
	}

	err = register_rpmsg_driver(&rpmsg_tty_rpmsg_drv);
	if (err < 0) {
		pr_err("Couldn't register rpmsg tty driver: err %d\n", err);
		goto error_unregister;
	}

	return 0;

error_unregister:
	tty_unregister_driver(rpmsg_tty_driver);

error_put:
	put_tty_driver(rpmsg_tty_driver);

	return err;
}

static void __exit rpmsg_tty_exit(void)
{
	unregister_rpmsg_driver(&rpmsg_tty_rpmsg_drv);
	tty_unregister_driver(rpmsg_tty_driver);
	put_tty_driver(rpmsg_tty_driver);
	idr_destroy(&tty_idr);
}

module_init(rpmsg_tty_init);
module_exit(rpmsg_tty_exit);

MODULE_AUTHOR("Arnaud Pouliquen <arnaud.pouliquen@st.com>");
MODULE_AUTHOR("Fabien Dessenne <fabien.dessenne@st.com>");
MODULE_DESCRIPTION("virtio remote processor messaging tty driver");
MODULE_LICENSE("GPL v2");
