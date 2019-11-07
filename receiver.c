// Copyright 2016 Alexander Palaistras. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.

#include <stdio.h>
#include <stdbool.h>

#include <main/php.h>
#include <zend_exceptions.h>
#include <ext/standard/php_string.h>

#include "value.h"
#include "receiver.h"
#include "_cgo_export.h"

struct engine_receiver {
	zend_object obj;
};

char *receiver_get_name(struct engine_receiver *rcvr) {
    return (char *) rcvr->obj.ce->name;
}

// Fetch and return field for method receiver.
static zval *receiver_get(zval *object, zval *member, int type, const zend_literal *key TSRMLS_DC) {
	struct engine_receiver *this = (struct engine_receiver *) zend_object_store_get_object(object TSRMLS_CC);
	zval *val = NULL;

	engine_value *result = engineReceiverGet(this, Z_STRVAL_P(member));
	if (result == NULL) {
		MAKE_STD_ZVAL(val);
		ZVAL_NULL(val);

		return val;
	}

	val = value_copy(result->value);
	value_destroy(result);

	return val;
}

// Set field for method receiver.
static void receiver_set(zval *object, zval *member, zval *value, const zend_literal *key TSRMLS_DC) {
	struct engine_receiver *this = (struct engine_receiver *) zend_object_store_get_object(object TSRMLS_CC);
	engineReceiverSet(this, Z_STRVAL_P(member), (void *) value);
}

// Check if field exists for method receiver.
static int receiver_exists(zval *object, zval *member, int check, const zend_literal *key TSRMLS_DC) {
	struct engine_receiver *this = (struct engine_receiver *) zend_object_store_get_object(object TSRMLS_CC);

	if (!engineReceiverExists(this, Z_STRVAL_P(member))) {
		// Value does not exist.
		return 0;
	} else if (check == 2) {
		// Value exists.
		return 1;
	}

	int result = 0;
	engine_value *val = engineReceiverGet(this, Z_STRVAL_P(member));

	if (check == 1) {
		// Value exists and is "truthy".
		convert_to_boolean(val->value);
		result = (Z_BVAL_P(val->value)) ? 1 : 0;
	} else if (check == 0) {
		// Value exists and is not null.
		result = (val->kind != KIND_NULL) ? 1 : 0;
	} else {
		// Check value is invalid.
		result = 0;
	}

	value_destroy(val);
	return result;
}

// Call function with arguments passed and return value (if any).
static int receiver_method_call(const char *m, INTERNAL_FUNCTION_PARAMETERS) {
	struct engine_receiver *this = (struct engine_receiver *) zend_object_store_get_object(getThis() TSRMLS_CC);
	zend_internal_function *func = (zend_internal_function *) EG(current_execute_data)->function_state.function;
	zval args;

	array_init_size(&args, ZEND_NUM_ARGS());

	if (zend_copy_parameters_array(ZEND_NUM_ARGS(), &args TSRMLS_CC) == FAILURE) {
		zval_dtor(&args);
		RETURN_NULL();
	}

	engine_value *result = engineReceiverCall(this, m, (void *) &args);
	if (result == NULL) {
		zval_dtor(&args);
		RETURN_NULL();
	}

	zval_dtor(&args);

	zval *val = value_copy(result->value);
	value_destroy(result);

	RETURN_ZVAL(val, 0, 0);
}

// Create new method receiver instance and attach to instantiated PHP object.
// Returns an exception if method receiver failed to initialize for any reason.
static void receiver_new(INTERNAL_FUNCTION_PARAMETERS) {
	struct engine_receiver *this = (struct engine_receiver *) zend_object_store_get_object(getThis() TSRMLS_CC);
	zval args;

	array_init_size(&args, ZEND_NUM_ARGS());

	if (zend_copy_parameters_array(ZEND_NUM_ARGS(), &args TSRMLS_CC) == FAILURE) {
		zend_throw_exception(NULL, "Could not parse parameters for method receiver", 0 TSRMLS_CC);
		zval_dtor(&args);
		return;
	}

	// Create receiver instance. Throws an exception if creation fails.
	int result = engineReceiverNew(this, (void *) &args);
	if (result != 0) {
		zend_throw_exception(NULL, "Failed to instantiate method receiver", 0 TSRMLS_CC);
		zval_dtor(&args);
		return;
	}

	zval_dtor(&args);
}

// Fetch and return function definition for method receiver. The method call
// happens in the method handler, as returned by this function.
static zend_function *receiver_method_get(zval **object_ptr, char *name, int len, const zend_literal *key TSRMLS_DC) {
	struct engine_receiver *this = (struct engine_receiver *) zend_object_store_get_object(*object_ptr TSRMLS_CC);
	zend_internal_function *func = emalloc(sizeof(zend_internal_function));

	func->type     = ZEND_OVERLOADED_FUNCTION;
	func->handler  = NULL;
	func->arg_info = NULL;
	func->num_args = 0;
	func->scope    = this->obj.ce;
	func->fn_flags = ZEND_ACC_CALL_VIA_HANDLER;
	func->function_name = estrndup(name, len);

	return (zend_function *) func;
}

// Fetch and return constructor function definition for method receiver. The
// construct call happens in the constructor handler, as returned by this
// function.
static zend_function *receiver_constructor_get(zval *object TSRMLS_DC) {
	struct engine_receiver *this = (struct engine_receiver *) zend_object_store_get_object(object TSRMLS_CC);
	static zend_internal_function func;

	func.type     = ZEND_INTERNAL_FUNCTION;
	func.handler  = receiver_new;
	func.arg_info = NULL;
	func.num_args = 0;
	func.scope    = this->obj.ce;
	func.fn_flags = 0;
	func.function_name = (char *) this->obj.ce->name;

	return (zend_function *) &func;
}

static zend_class_entry *receiver_entry(const zval *object TSRMLS_DC) {
	struct engine_receiver *this = (struct engine_receiver *) zend_object_store_get_object(object TSRMLS_CC);

	return this->obj.ce;
}

static int receiver_name(const zval *object, const char **name, zend_uint *len, int parent TSRMLS_DC) {
	struct engine_receiver *this = (struct engine_receiver *) zend_object_store_get_object(object TSRMLS_CC);

	if (parent) {
		return FAILURE;
	}

	*len = this->obj.ce->name_length;
	*name = estrndup(this->obj.ce->name, this->obj.ce->name_length);

	return SUCCESS;
}

static zend_object_handlers receiver_handlers = {
	ZEND_OBJECTS_STORE_HANDLERS,

	receiver_get,            // read_property
	receiver_set,            // write_property
	NULL,                    // read_dimension
	NULL,                    // write_dimension

	NULL,                    // get_property_ptr_ptr
	NULL,                    // get
	NULL,                    // set

	receiver_exists,         // has_property
	NULL,                    // unset_property
	NULL,                    // has_dimension
	NULL,                    // unset_dimension

	NULL,                    // get_properties

	receiver_method_get,     // get_method
	receiver_method_call,    // call_method

	receiver_constructor_get // get_constructor
};

// Free storage for allocated method receiver instance.
static void receiver_free(void *object TSRMLS_DC) {
	struct engine_receiver *this = (struct engine_receiver *) object;

	zend_object_std_dtor(&this->obj TSRMLS_CC);
	efree(this);
}

// Initialize instance of method receiver object. The method receiver itself is
// attached in the constructor function call.
static zend_object_value receiver_init(zend_class_entry *class_type TSRMLS_DC) {
	struct engine_receiver *this;
	zend_object_value object;

	this = emalloc(sizeof(struct engine_receiver));
	memset(this, 0, sizeof(struct engine_receiver));

	zend_object_std_init(&this->obj, class_type TSRMLS_CC);

	object.handle = zend_objects_store_put(this, (zend_objects_store_dtor_t) zend_objects_destroy_object, (zend_objects_free_object_storage_t) receiver_free, NULL TSRMLS_CC);
	object.handlers = &receiver_handlers;

	return object;
}

static zend_function_entry gophp_function_entry[] = {
	PHP_FE_END
};

static char **receiver_names;
static unsigned int receiver_len;

ZEND_MINIT_FUNCTION(gophp)
{
	int i;
	for (i = 0; i < receiver_len; i++) {
		zend_class_entry ce;
		INIT_CLASS_ENTRY_EX(ce, receiver_names[i], strlen(receiver_names[i]), NULL);

		ce.create_object = receiver_init;
		zend_class_entry *this = zend_register_internal_class(&ce TSRMLS_CC);
	}

	// Set standard handlers for receiver.
	zend_object_handlers *std = zend_get_std_object_handlers();
	receiver_handlers.get_class_name  = std->get_class_name;
	receiver_handlers.get_class_entry = std->get_class_entry;

	return SUCCESS;
}

zend_module_entry gophp_module_entry = {
	STANDARD_MODULE_HEADER,
	"gophp",                    /* extension name */
	gophp_function_entry,       /* function list */
	ZEND_MINIT(gophp),          /* process startup */
	NULL,                       /* process shutdown */
	NULL,                       /* request startup */
	NULL,                       /* request shutdown */
	NULL,                       /* extension info */
	"0.12",                     /* extension version */
	STANDARD_MODULE_PROPERTIES
};

void receiver_module_init(int n_receivers) {
	receiver_names = (char **) calloc(n_receivers, sizeof(char*));
}

void receiver_define(char *name) {
	receiver_names[receiver_len++] = strdup(name);
}
