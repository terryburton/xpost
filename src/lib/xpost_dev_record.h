/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * All rights reserved.
 * (BSD 3-clause; see COPYING)
 */

#ifndef XPOST_DEV_RECORD_H
#define XPOST_DEV_RECORD_H

/**
 * @file xpost_dev_record.h
 * @brief The device that writes a page down instead of painting it.
 *
 * Installs loadrecorddevice in systemdict, which when run creates
 *
 *       width height  newrecorddevice  device
 *
 * and the replay operator the recorded page is painted through.
 */
int xpost_oper_init_record_device_ops(Xpost_Context *ctx,
                                      Xpost_Object sd);

#endif
