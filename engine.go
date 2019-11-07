// Copyright 2016 Alexander Palaistras. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.

package php

// #cgo CFLAGS: -I/usr/include/php -I/usr/include/php/main -I/usr/include/php/TSRM
// #cgo CFLAGS: -I/usr/include/php/Zend
// #cgo LDFLAGS: -lphp5
//
// #include <stdlib.h>
//
// #include "receiver.h"
// #include "context.h"
// #include "engine.h"
import "C"

import (
	"fmt"
	"io"
	"net/http"
	"strings"
	"unsafe"
)

// Engine represents the core PHP engine bindings.
type Engine struct {
	engine    *C.struct_php_engine
	contexts  map[*C.struct_engine_context]*Context
	receivers map[string]*Receiver
}

// This contains a reference to the active engine, if any.
var engine *Engine

type ReceiverNewFn = func(args []interface{}) interface{}

// New initializes a PHP engine instance on which contexts can be executed. It
// corresponds to PHP's MINIT (module init) phase.
//
// Takes a map of receiver->constructor which will be registered PHP classes
// under the go-php module.
func New(receivers map[string]ReceiverNewFn) (*Engine, error) {
	if engine != nil {
		return nil, fmt.Errorf("Cannot activate multiple engine instances")
	}

	engine = &Engine{
		contexts:  make(map[*C.struct_engine_context]*Context),
		receivers: make(map[string]*Receiver),
	}

	C.receiver_module_init(C.int(len(receivers)))
	for rcvrName, newFn := range receivers {
		engine.receivers[rcvrName] = NewReceiver(rcvrName, newFn)
		name := C.CString(rcvrName)
		C.receiver_define(name)
		C.free(unsafe.Pointer(name))
	}

	var err error
	engine.engine, err = C.engine_init()
	if err != nil {
		return nil, fmt.Errorf("PHP engine failed to initialize")
	}

	return engine, nil
}

// NewContext creates a new execution context for the active engine and returns
// an error if the execution context failed to initialize at any point. This
// corresponds to PHP's RINIT (request init) phase.
func (e *Engine) NewContext() (*Context, error) {
	ptr, err := C.context_new()
	if err != nil {
		return nil, fmt.Errorf("Failed to initialize context for PHP engine")
	}

	ctx := &Context{
		Header:  make(http.Header),
		context: ptr,
		values:  make(map[string]*Value, 0),
	}

	// Store reference to context, using pointer as key.
	e.contexts[ptr] = ctx

	return ctx, nil
}

// Destroy shuts down and frees any resources related to the PHP engine bindings.
func (e *Engine) Destroy() {
	if e.engine == nil {
		return
	}

	C.engine_shutdown(e.engine)

	for _, r := range e.receivers {
		r.Destroy()
	}

	e.receivers = nil

	for _, c := range e.contexts {
		c.Destroy()
	}

	e.contexts = nil

	e.engine = nil

	engine = nil
}

func write(w io.Writer, buffer unsafe.Pointer, length C.uint) C.int {
	// Do not return error if writer is unavailable.
	if w == nil {
		return C.int(length)
	}

	written, err := w.Write(C.GoBytes(buffer, C.int(length)))
	if err != nil {
		return -1
	}

	return C.int(written)
}

//export engineWriteOut
func engineWriteOut(ctx *C.struct_engine_context, buffer unsafe.Pointer, length C.uint) C.int {
	if engine == nil || engine.contexts[ctx] == nil {
		return -1
	}

	return write(engine.contexts[ctx].Output, buffer, length)
}

//export engineWriteLog
func engineWriteLog(ctx *C.struct_engine_context, buffer unsafe.Pointer, length C.uint) C.int {
	if engine == nil || engine.contexts[ctx] == nil {
		return -1
	}

	return write(engine.contexts[ctx].Log, buffer, length)
}

//export engineSetHeader
func engineSetHeader(ctx *C.struct_engine_context, operation C.uint, buffer unsafe.Pointer, length C.uint) {
	if engine == nil || engine.contexts[ctx] == nil {
		return
	}

	header := (string)(C.GoBytes(buffer, C.int(length)))
	split := strings.SplitN(header, ":", 2)

	for i := range split {
		split[i] = strings.TrimSpace(split[i])
	}

	switch operation {
	case 0: // Replace header.
		if len(split) == 2 && split[1] != "" {
			engine.contexts[ctx].Header.Set(split[0], split[1])
		}
	case 1: // Append header.
		if len(split) == 2 && split[1] != "" {
			engine.contexts[ctx].Header.Add(split[0], split[1])
		}
	case 2: // Delete header.
		if split[0] != "" {
			engine.contexts[ctx].Header.Del(split[0])
		}
	}
}

//export engineReceiverNew
func engineReceiverNew(rcvr *C.struct_engine_receiver, args unsafe.Pointer) C.int {
	n := C.GoString(C.receiver_get_name(rcvr))
	if engine == nil || engine.receivers[n] == nil {
		return 1
	}

	va, err := NewValueFromPtr(args)
	if err != nil {
		return 1
	}

	defer va.Destroy()

	obj, err := engine.receivers[n].NewObject(va.Slice())
	if err != nil {
		return 1
	}

	engine.receivers[n].objects[rcvr] = obj

	return 0
}

//export engineReceiverGet
func engineReceiverGet(rcvr *C.struct_engine_receiver, name *C.char) unsafe.Pointer {
	n := C.GoString(C.receiver_get_name(rcvr))
	if engine == nil || engine.receivers[n].objects[rcvr] == nil {
		return nil
	}

	val, err := engine.receivers[n].objects[rcvr].Get(C.GoString(name))
	if err != nil {
		return nil
	}

	return val.Ptr()
}

//export engineReceiverSet
func engineReceiverSet(rcvr *C.struct_engine_receiver, name *C.char, val unsafe.Pointer) {
	n := C.GoString(C.receiver_get_name(rcvr))
	if engine == nil || engine.receivers[n].objects[rcvr] == nil {
		return
	}

	v, err := NewValueFromPtr(val)
	if err != nil {
		return
	}

	engine.receivers[n].objects[rcvr].Set(C.GoString(name), v.Interface())
}

//export engineReceiverExists
func engineReceiverExists(rcvr *C.struct_engine_receiver, name *C.char) C.int {
	n := C.GoString(C.receiver_get_name(rcvr))
	if engine == nil || engine.receivers[n].objects[rcvr] == nil {
		return 0
	}

	if engine.receivers[n].objects[rcvr].Exists(C.GoString(name)) {
		return 1
	}

	return 0
}

//export engineReceiverCall
func engineReceiverCall(rcvr *C.struct_engine_receiver, name *C.char, args unsafe.Pointer) unsafe.Pointer {
	n := C.GoString(C.receiver_get_name(rcvr))
	if engine == nil || engine.receivers[n].objects[rcvr] == nil {
		return nil
	}

	// Process input arguments
	va, err := NewValueFromPtr(args)
	if err != nil {
		return nil
	}

	defer va.Destroy()

	val := engine.receivers[n].objects[rcvr].Call(C.GoString(name), va.Slice())
	if val == nil {
		return nil
	}

	return val.Ptr()
}
