.. SPDX-License-Identifier: GPL-2.0

=============
The rpmsg TTY
=============

The rpmsg tty driver implements a serial communication on the rpmsg bus,
to communicate with a remote processor devices in asymmetric multiprocessing
(AMP) configurations.

The remote processor can instantiate a new tty by requesting a new "rpmsg-tty-channel" RPMsg service. Information related to the RPMsg and
associated tty device is available in /sys/bus/rpmsg/devices/virtio0.rpmsg-tty-channel.-1.<X>, with
<X> corresponding to the ttyRPMSG instance.

RPMsg data/control structure
----------------------------

The RPMsg is used to send data or control messages. Differentiation between the
stream and the control messages is done thanks to the first byte of the
RPMsg payload:


- RPMSG_DATA	- rest of messages contains data

- RPMSG_CTRL 	- message contains control.


To be compliant with this driver, the remote firmware has to respect this RPMsg
payload structure. At least the RPMSG_DATA type has to be supported. The
RPMSG_CTRL is optional.

Flow control type
-----------------

A minimum flow control can be implemented to allow/block communication with the remote processor.

- DATA_TERM_READY
  one asocated parameter:

  - u8 state:

    - Set to indicate to remote side that terminal is ready for communication.
    - Reset to block communication with remote side.
