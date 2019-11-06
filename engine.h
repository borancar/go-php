// Copyright 2015 Alexander Palaistras. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.

#ifndef __ENGINE_H__
#define __ENGINE_H__

struct php_engine;

struct php_engine *engine_init(void);
void engine_shutdown(struct php_engine *engine);

#endif
