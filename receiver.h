// Copyright 2016 Alexander Palaistras. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.

#ifndef __RECEIVER_H__
#define __RECEIVER_H__

struct engine_receiver;

void receiver_define(char *name);
void receiver_destroy(char *name);
char *receiver_get_name(struct engine_receiver *rcvr);

#endif
