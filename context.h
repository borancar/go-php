// Copyright 2015 Alexander Palaistras. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.

#ifndef __CONTEXT_H__
#define __CONTEXT_H__

struct engine_context;

struct engine_context *context_new();
void context_exec(struct engine_context *context, char *filename);
void *context_eval(struct engine_context *context, char *script);
void context_bind(struct engine_context *context, char *name, void *value);
void context_destroy(struct engine_context *context);

#endif
