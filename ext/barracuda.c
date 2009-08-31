#include <ruby.h>
#include <OpenCL/OpenCL.h>

static VALUE rb_mBarracuda;
static VALUE rb_cBuffer;
static VALUE rb_cOutputBuffer;
static VALUE rb_cProgram;
static VALUE rb_eProgramSyntaxError;
static VALUE rb_eOpenCLError;
static VALUE rb_cType;
static VALUE rb_hTypes;

static ID ba_worker_size;
static ID id_to_s;
static ID id_data_type;
static ID id_object;

static ID id_type_bool;
static ID id_type_char;
static ID id_type_uchar;
static ID id_type_short;
static ID id_type_ushort;
static ID id_type_int;
static ID id_type_uint;
static ID id_type_long;
static ID id_type_ulong;
static ID id_type_float;
static ID id_type_half;
static ID id_type_size_t;
static ID id_type_ptrdiff_t;
static ID id_type_intptr_t;
static ID id_type_uintptr_t;
/*static ID id_type_void;*/

static VALUE program_compile(VALUE self, VALUE source);
static VALUE buffer_data_set(VALUE self, VALUE new_value);

static cl_device_id device_id = NULL;
static cl_context context = NULL;
static int err;

#define VERSION_STRING "1.0"

enum buffer_type {
    BUFFER_TYPE_BOOL = 0x01,
    BUFFER_TYPE_CHAR = 0x02,
    BUFFER_TYPE_UCHAR = 0x03,
    BUFFER_TYPE_SHORT = 0x04,
    BUFFER_TYPE_USHORT = 0x05,
    BUFFER_TYPE_INT = 0x06,
    BUFFER_TYPE_UINT = 0x07,
    BUFFER_TYPE_LONG = 0x08,
    BUFFER_TYPE_ULONG = 0x09,
    BUFFER_TYPE_FLOAT = 0x0A,
    BUFFER_TYPE_HALF = 0x0B,
    BUFFER_TYPE_SIZET = 0x0C,
    BUFFER_TYPE_PTRDIFFT = 0x0D,
    BUFFER_TYPE_INTPTRT = 0x0E,
    BUFFER_TYPE_UINTPTRT = 0x0F,
    BUFFER_TYPE_VOID = 0x10
};

struct program {
    cl_program program;
};

struct kernel {
    cl_kernel kernel;
};

struct buffer {
    VALUE arr;
    enum buffer_type type;
    size_t num_items;
    size_t member_size;
    void *cachebuf;
    cl_mem data;
};

static VALUE
data_type_set(VALUE self, VALUE value)
{
    if (TYPE(value) != T_SYMBOL) {
        value = rb_str_intern(rb_String(value));
    }
    if (rb_hash_aref(rb_hTypes, value) == Qnil) {
        rb_raise(rb_eArgError, "invalid data type %s", 
            RSTRING_PTR(rb_inspect(value)));
    }
    
    rb_ivar_set(self, id_data_type, value);
    return self;
}

static VALUE
data_type_get(VALUE self, ID type)
{
    VALUE value = rb_ivar_get(self, id_data_type);
    if (NIL_P(value)) {
        value = ID2SYM(type);
        data_type_set(self, value);
    }
    return value;
}

static VALUE
object_data_type_get(VALUE self)
{
    return rb_ivar_get(self, id_data_type);
}

static VALUE
fixnum_data_type_get(VALUE self)
{
    return ID2SYM(id_type_int);
}

static VALUE
bignum_data_type_get(VALUE self)
{
    return data_type_get(self, id_type_long);
}

static VALUE
float_data_type_get(VALUE self)
{
    return data_type_get(self, id_type_float);
}

static VALUE
array_data_type_get(VALUE self)
{
    VALUE value = rb_ivar_get(self, id_data_type);
    if (NIL_P(value)) {
        if (RARRAY_LEN(self) == 0) goto error;

        value = rb_funcall(RARRAY_PTR(self)[0], id_data_type, 0);
        if (NIL_P(value)) goto error;
        data_type_set(self, value);
    }
    return value;
    
error:
    rb_raise(rb_eRuntimeError, "unknown buffer data in array %s", 
        RSTRING_PTR(rb_inspect(self)));
}

#define GET_PROGRAM() \
    struct program *program; \
    Data_Get_Struct(self, struct program, program);
    
#define GET_BUFFER() \
    struct buffer *buffer; \
    Data_Get_Struct(self, struct buffer, buffer);

#define TYPE_SET(type, size) \
    id_type_##type = rb_intern(#type); \
    rb_hash_aset(rb_hTypes, ID2SYM(id_type_##type), INT2FIX(sizeof(size)));

#define TYPE_TO_NATIVE(type_name, cast_type, CONVERT_FUNC) \
    if (id_type_##type_name == data_type) { \
        *((cast_type*)native_value) = (cast_type)CONVERT_FUNC(value); \
        return; \
    }

#define TYPE_TO_RUBY(type_name, cast_type, CONVERT_FUNC) \
    if (id_type_##type_name == data_type) { \
        return CONVERT_FUNC(*((cast_type*)native_value)); \
    }

static void
types_hash_init()
{
    TYPE_SET(bool,      char);
    TYPE_SET(char,      cl_char);
    TYPE_SET(uchar,     cl_uchar);
    TYPE_SET(short,     cl_short);
    TYPE_SET(ushort,    cl_ushort);
    TYPE_SET(int,       cl_int);
    TYPE_SET(uint,      cl_uint);
    TYPE_SET(long,      cl_long);
    TYPE_SET(ulong,     cl_ulong);
    TYPE_SET(float,     cl_float);
    TYPE_SET(half,      cl_half);
    TYPE_SET(size_t,    size_t);
    TYPE_SET(ptrdiff_t, ptrdiff_t);
    TYPE_SET(intptr_t,  intptr_t);
    TYPE_SET(uintptr_t, uintptr_t);
    OBJ_FREEZE(rb_hTypes);
}

static void
type_to_native(VALUE value, ID data_type, void *native_value)
{
    TYPE_TO_NATIVE(bool,      char,       FIX2INT);
    TYPE_TO_NATIVE(char,      cl_char,   *RSTRING_PTR);
    TYPE_TO_NATIVE(uchar,     cl_uchar,  *RSTRING_PTR);
    TYPE_TO_NATIVE(short,     cl_short,   FIX2INT);
    TYPE_TO_NATIVE(ushort,    cl_ushort,  NUM2UINT);
    TYPE_TO_NATIVE(int,       cl_int,     FIX2INT);
    TYPE_TO_NATIVE(uint,      cl_uint,    NUM2UINT);
    TYPE_TO_NATIVE(long,      cl_long,    NUM2LONG);
    TYPE_TO_NATIVE(ulong,     cl_ulong,   NUM2ULONG);
    TYPE_TO_NATIVE(float,     cl_float,   RFLOAT_VALUE);
    TYPE_TO_NATIVE(half,      cl_half,    RFLOAT_VALUE);
    TYPE_TO_NATIVE(size_t,    size_t,     NUM2ULONG);
    TYPE_TO_NATIVE(ptrdiff_t, ptrdiff_t,  NUM2LONG);
    TYPE_TO_NATIVE(intptr_t,  intptr_t,   FIX2INT);
    TYPE_TO_NATIVE(uintptr_t, uintptr_t,  NUM2UINT);
}

static VALUE
type_to_ruby(void *native_value, ID data_type)
{
    TYPE_TO_RUBY(bool,      char,       INT2FIX);
    TYPE_TO_RUBY(char,      cl_char*,   rb_str_new2);
    TYPE_TO_RUBY(uchar,     cl_uchar*,  rb_str_new2);
    TYPE_TO_RUBY(short,     cl_short,   INT2FIX);
    TYPE_TO_RUBY(ushort,    cl_ushort,  UINT2NUM);
    TYPE_TO_RUBY(int,       cl_int,     INT2FIX);
    TYPE_TO_RUBY(uint,      cl_uint,    UINT2NUM);
    TYPE_TO_RUBY(long,      cl_long,    LONG2NUM);
    TYPE_TO_RUBY(ulong,     cl_ulong,   ULONG2NUM);
    TYPE_TO_RUBY(float,     cl_float,   rb_float_new);
    TYPE_TO_RUBY(half,      cl_half,    rb_float_new);
    TYPE_TO_RUBY(size_t,    size_t,     ULONG2NUM);
    TYPE_TO_RUBY(ptrdiff_t, ptrdiff_t,  LONG2NUM);
    TYPE_TO_RUBY(intptr_t,  intptr_t,   INT2FIX);
    TYPE_TO_RUBY(uintptr_t, uintptr_t,  UINT2NUM);
}

static VALUE
type_initialize(VALUE self, VALUE object)
{
    rb_ivar_set(self, id_object, object);
    return self;
}

static VALUE
type_method_missing(VALUE self, VALUE type)
{
    data_type_set(self, type);
    return self;
}

static VALUE
type_object(VALUE self)
{
    return rb_ivar_get(self, id_object);
}

static VALUE
object_to_type(VALUE self, VALUE type)
{
    VALUE out = rb_funcall(rb_cType, rb_intern("new"), 1, self);
    return type_method_missing(out, type);
}

static VALUE
type_new(VALUE klass, VALUE type)
{
    return rb_funcall(rb_cType, rb_intern("new"), 1, type);
}

static void
free_buffer(struct buffer *buffer)
{
    fflush(stdout);
    clReleaseMemObject(buffer->data);
    rb_gc_mark(buffer->arr);
    ruby_xfree(buffer->cachebuf);
    ruby_xfree(buffer);
}

static VALUE
buffer_s_allocate(VALUE klass)
{
    struct buffer *buffer;
    buffer = ALLOC(struct buffer);
    MEMZERO(buffer, struct buffer, 1);
    buffer->arr = Qnil;
    return Data_Wrap_Struct(klass, 0, free_buffer, buffer);
}

static void
buffer_update_cache_info(struct buffer *buffer)
{
    buffer->num_items = RARRAY_LEN(buffer->arr);

    switch (TYPE(RARRAY_PTR(buffer->arr)[0])) {
        case T_FIXNUM:
            buffer->type = BUFFER_TYPE_INT;
            buffer->member_size = sizeof(int);
            break;
        case T_FLOAT:
            buffer->type = BUFFER_TYPE_FLOAT;
            buffer->member_size = sizeof(float);
            break;
        default:
            rb_raise(rb_eRuntimeError, "invalid buffer data %s", 
                RSTRING_PTR(rb_inspect(buffer->arr)));
    }
}

#define WRITE_TYPE(type, conv_type, M) \
    case BUFFER_TYPE_##type: {\
        conv_type value = M(item); \
        ((conv_type*)buffer->cachebuf)[i] = value; \
        break; \
    }

static VALUE
buffer_write(VALUE self)
{
    unsigned int i;
    
    GET_BUFFER();
    
    buffer_update_cache_info(buffer);
    
    if (buffer->cachebuf) {
        xfree(buffer->cachebuf);
    }
    buffer->cachebuf = malloc(buffer->num_items * buffer->member_size);
    
    for (i = 0; i < RARRAY_LEN(buffer->arr); i++) {
        VALUE item = RARRAY_PTR(buffer->arr)[i];
        switch (buffer->type) {
            WRITE_TYPE(BOOL,     int,        FIX2INT);
            WRITE_TYPE(CHAR,     cl_char,   *RSTRING_PTR);
            WRITE_TYPE(UCHAR,    cl_uchar,  *RSTRING_PTR);
            WRITE_TYPE(SHORT,    cl_short,   FIX2INT);
            WRITE_TYPE(USHORT,   cl_ushort,  NUM2UINT);
            WRITE_TYPE(INT,      cl_int,     FIX2INT);
            WRITE_TYPE(UINT,     cl_uint,    NUM2UINT);
            WRITE_TYPE(LONG,     cl_long,    NUM2LONG);
            WRITE_TYPE(ULONG,    cl_ulong,   NUM2ULONG);
            WRITE_TYPE(FLOAT,    cl_float,   RFLOAT_VALUE);
            WRITE_TYPE(HALF,     cl_half,    RFLOAT_VALUE);
            WRITE_TYPE(SIZET,    size_t,     NUM2ULONG);
            WRITE_TYPE(PTRDIFFT, ptrdiff_t,  NUM2LONG);
            WRITE_TYPE(INTPTRT,  intptr_t,   FIX2INT);
            WRITE_TYPE(UINTPTRT, uintptr_t,  NUM2UINT);

            default:
                ((uint32_t *)buffer->cachebuf)[i] = 0;
        }       
    }
    
    return self;
}

#define READ_TYPE(type, conv_type, M) \
    case BUFFER_TYPE_##type: \
        rb_ary_push(buffer->arr, M(((conv_type*)buffer->cachebuf)[i])); \
        break;

static VALUE
buffer_read(VALUE self)
{
    unsigned int i;
    
    GET_BUFFER();
    
    rb_gc_mark(buffer->arr);
    buffer->arr = rb_ary_new2(buffer->num_items);

    for (i = 0; i < buffer->num_items; i++) {
        switch (buffer->type) {
            READ_TYPE(BOOL,     int,        INT2FIX);
            READ_TYPE(CHAR,     cl_char*,   rb_str_new2);
            READ_TYPE(UCHAR,    cl_uchar*,  rb_str_new2);
            READ_TYPE(SHORT,    cl_short,   INT2FIX);
            READ_TYPE(USHORT,   cl_ushort,  UINT2NUM);
            READ_TYPE(INT,      cl_int,     INT2FIX);
            READ_TYPE(UINT,     cl_uint,    UINT2NUM);
            READ_TYPE(LONG,     cl_long,    LONG2NUM);
            READ_TYPE(ULONG,    cl_ulong,   ULONG2NUM);
            READ_TYPE(FLOAT,    cl_float,   rb_float_new);
            READ_TYPE(HALF,     cl_half,    rb_float_new);
            READ_TYPE(SIZET,    size_t,     ULONG2NUM);
            READ_TYPE(PTRDIFFT, ptrdiff_t,  LONG2NUM);
            READ_TYPE(INTPTRT,  intptr_t,   INT2FIX);
            READ_TYPE(UINTPTRT, uintptr_t,  UINT2NUM);

            default:
                rb_ary_push(buffer->arr, Qnil);
        }       
    }
    
    return self;
}

static VALUE
buffer_size_changed(VALUE self)
{
    GET_BUFFER();
    
    if (buffer->data) {
        clReleaseMemObject(buffer->data);
    }
    buffer_update_cache_info(buffer);
    buffer->data = clCreateBuffer(context, CL_MEM_READ_WRITE, 
        buffer->num_items * buffer->member_size, NULL, NULL);

    buffer_write(self);
    
    return self;
}

static VALUE
buffer_data(VALUE self)
{
    GET_BUFFER();
    return buffer->arr;
}

static VALUE
buffer_data_set(VALUE self, VALUE new_value)
{
    GET_BUFFER();
    
    if (RTEST(buffer->arr)) {
        rb_gc_mark(buffer->arr);
    }
    buffer->arr = new_value;
    buffer_size_changed(self);
    return buffer->arr;
}

static VALUE
buffer_initialize(int argc, VALUE *argv, VALUE self)
{
    GET_BUFFER();
    
    if (argc == 0) {
        rb_raise(rb_eArgError, "no buffer data given");
    }
    
    if (TYPE(argv[0]) == T_ARRAY) {
        buffer_data_set(self, argv[0]);
    }
    else {
        buffer_data_set(self, rb_ary_new4(argc, argv));
    }
    
    return self;
}

#define TYPE_CASE(t, t2, size) \
    if (id_type == id_type_##t) { \
        buffer->type = BUFFER_TYPE_##t2; \
        buffer->member_size = sizeof(size); \
        return type; \
    }

static VALUE
obuffer_type_set(VALUE self, VALUE type)
{
    ID id_type;
    GET_BUFFER();
    
    id_type = rb_intern_str(rb_funcall(type, id_to_s, 0));
    TYPE_CASE(bool,      BOOL,     int);
    TYPE_CASE(char,      CHAR,     cl_char);
    TYPE_CASE(uchar,     UCHAR,    cl_uchar);
    TYPE_CASE(short,     SHORT,    cl_short);
    TYPE_CASE(ushort,    USHORT,   cl_ushort);
    TYPE_CASE(int,       INT,      cl_int);
    TYPE_CASE(uint,      UINT,     cl_uint);
    TYPE_CASE(long,      LONG,     cl_long);
    TYPE_CASE(ulong,     ULONG,    cl_ulong);
    TYPE_CASE(float,     FLOAT,    cl_float);
    TYPE_CASE(half,      HALF,     cl_half);
    TYPE_CASE(size_t,    SIZET,    size_t);
    TYPE_CASE(ptrdiff_t, PTRDIFFT, ptrdiff_t);
    TYPE_CASE(intptr_t,  INTPTRT,  intptr_t);
    TYPE_CASE(uintptr_t, UINTPTRT, uintptr_t);

    rb_raise(rb_eArgError, "type can only be :float or :int");
}

static VALUE
obuffer_initialize(VALUE self, VALUE type, VALUE size)
{
    GET_BUFFER();
    
    obuffer_type_set(self, type);
    
    if (TYPE(size) != T_FIXNUM) {
        rb_raise(rb_eArgError, "expecting buffer size as argument 2");
    }
    
    buffer->num_items = FIX2UINT(size);
    buffer->cachebuf = malloc(buffer->num_items * buffer->member_size);
    buffer->data = clCreateBuffer(context, CL_MEM_READ_WRITE, 
        buffer->member_size * buffer->num_items, NULL, NULL);
    
    return self;
}

static VALUE
obuffer_clear(VALUE self)
{
    GET_BUFFER();
    memset(buffer->cachebuf, 0, buffer->member_size * buffer->num_items);
    return self;
}

static VALUE
obuffer_size(VALUE self)
{
    GET_BUFFER();
    return INT2FIX(buffer->num_items);
}

static void
free_program(struct program *program)
{
    clReleaseProgram(program->program);
    xfree(program);
}

static VALUE
program_s_allocate(VALUE klass)
{
    struct program *program;
    program = ALLOC(struct program);
    MEMZERO(program, struct program, 1);
    return Data_Wrap_Struct(klass, 0, free_program, program);
}

static VALUE
program_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE source;
    
    rb_scan_args(argc, argv, "01", &source);
    if (source != Qnil) {
        program_compile(self, source);
    }
    
    return self;
}

static VALUE
program_compile(VALUE self, VALUE source)
{
    const char *c_source;
    GET_PROGRAM();
    StringValue(source);
    
    if (program->program) {
        clReleaseProgram(program->program);
        program->program = 0;
    }
    
    c_source = StringValueCStr(source);
    program->program = clCreateProgramWithSource(context, 1, &c_source, NULL, &err);
    if (!program->program) {
        program->program = 0;
        rb_raise(rb_eOpenCLError, "failed to create compute program");
    }

    err = clBuildProgram(program->program, 0, NULL, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t len;
        char buffer[2048];

        clGetProgramBuildInfo(program->program, device_id, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
        clReleaseProgram(program->program);
        program->program = 0;
        rb_raise(rb_eProgramSyntaxError, "%s", buffer);
    }
    
    return Qtrue;
}

#define CLEAN() program_clean(kernel, commands);
#define ERROR(msg) if (err != CL_SUCCESS) { CLEAN(); rb_raise(rb_eOpenCLError, msg); }

static void
program_clean(cl_kernel kernel, cl_command_queue commands)
{
    clReleaseKernel(kernel);
    clReleaseCommandQueue(commands);
}

static VALUE
program_method_missing(int argc, VALUE *argv, VALUE self)
{
    int i;
    size_t local = 0, global = 0;
    cl_kernel kernel;
    cl_command_queue commands;
    GET_PROGRAM();
    
    StringValue(argv[0]);
    kernel = clCreateKernel(program->program, RSTRING_PTR(argv[0]), &err);
    if (!kernel || err != CL_SUCCESS) {
        rb_raise(rb_eNoMethodError, "no kernel method '%s'", RSTRING_PTR(argv[0]));
    }
    
    commands = clCreateCommandQueue(context, device_id, 0, &err);
    if (!commands) {
        clReleaseKernel(kernel);
        rb_raise(rb_eOpenCLError, "could not execute kernel method '%s'", RSTRING_PTR(argv[0]));
    }

    for (i = 1; i < argc; i++) {
        VALUE item = argv[i];
        err = !CL_SUCCESS;
        
        if (i == argc - 1 && TYPE(item) == T_HASH) {
            VALUE worker_size = rb_hash_aref(item, ID2SYM(ba_worker_size));
            if (RTEST(worker_size) && TYPE(worker_size) == T_FIXNUM) {
                global = FIX2UINT(worker_size);
            }
            else {
                CLEAN();
                rb_raise(rb_eArgError, "opts hash must be {:worker_size => INT_VALUE}, got %s",
                    RSTRING_PTR(rb_inspect(item)));
            }
            break;
        }
        
        if (TYPE(item) == T_ARRAY) {
            /* create buffer from arg */
            VALUE buf = buffer_s_allocate(rb_cBuffer);
            item = buffer_initialize(1, &item, buf);
        }

        if (CLASS_OF(item) == rb_cOutputBuffer) {
            struct buffer *buffer;
            Data_Get_Struct(item, struct buffer, buffer);
            err = clSetKernelArg(kernel, i - 1, sizeof(cl_mem), &buffer->data);
            if (buffer->num_items > global) {
                global = buffer->num_items;
            }
        }
        else if (CLASS_OF(item) == rb_cBuffer) {
            struct buffer *buffer;
            Data_Get_Struct(item, struct buffer, buffer);

            buffer_write(item);
            clEnqueueWriteBuffer(commands, buffer->data, CL_TRUE, 0, 
                buffer->num_items * buffer->member_size, buffer->cachebuf, 0, NULL, NULL);
            err = clSetKernelArg(kernel, i - 1, sizeof(cl_mem), &buffer->data);
            if (buffer->num_items > global) {
                global = buffer->num_items;
            }
        }
        else {
            unsigned long data_ptr[16]; // a buffer of data
            size_t data_size_t;
            VALUE data_type, data_size;
            
            if (CLASS_OF(item) == rb_cType) {
                data_type = rb_funcall(type_object(item), id_data_type, 0);
            }
            else {
                data_type = rb_funcall(item, id_data_type, 0);
            }
            data_size = rb_hash_aref(rb_hTypes, data_type);
            if (NIL_P(data_size)) {
                CLEAN();
                rb_raise(rb_eRuntimeError, "invalid data type for %s", 
                    RSTRING_PTR(rb_inspect(item)));
            }
            
            data_size_t = FIX2UINT(data_size);
            type_to_native(item, SYM2ID(data_type), (void *)data_ptr);
            err = clSetKernelArg(kernel, i - 1, data_size_t, data_ptr);
        }

        if (err != CL_SUCCESS) {
            CLEAN();
            rb_raise(rb_eArgError, "invalid kernel method parameter: %s", RSTRING_PTR(rb_inspect(item)));
        }
    }
    
    err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &local, NULL);
    ERROR("failed to retrieve kernel work group info");
    
    { /* global work size must be power of 2, greater than 3 and not smaller than local */
        size_t size = 4;
        while (size < global) size *= 2;
        global = size;
        if (global < local) global = local;
    }

    clEnqueueNDRangeKernel(commands, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
    if (err) { CLEAN(); rb_raise(rb_eOpenCLError, "failed to execute kernel method"); }
    
    clFinish(commands);
    
    for (i = 1; i < argc; i++) {
        VALUE item = argv[i];
        if (CLASS_OF(item) == rb_cOutputBuffer) {
            struct buffer *buffer;
            Data_Get_Struct(item, struct buffer, buffer);
            err = clEnqueueReadBuffer(commands, buffer->data, CL_TRUE, 0, 
                buffer->num_items * buffer->member_size, buffer->cachebuf, 0, NULL, NULL);
            ERROR("failed to read output buffer");
            buffer_read(item);
        }
    }

    CLEAN();
    return Qnil;
}

static void
init_opencl()
{
    if (device_id == NULL) {
        err = clGetDeviceIDs(NULL, CL_DEVICE_TYPE_GPU, 1, &device_id, NULL);
        if (err != CL_SUCCESS) {
            rb_raise(rb_eOpenCLError, "failed to create a device group");
        }
    }

    if (context == NULL) {
        context = clCreateContext(0, 1, &device_id, NULL, NULL, &err);
        if (!context) {
            rb_raise(rb_eOpenCLError, "failed to create a program context");
        }
    }
}

void
Init_barracuda()
{
    ba_worker_size = rb_intern("worker_size");
    id_to_s = rb_intern("to_s");
    id_data_type = rb_intern("data_type");
    id_object = rb_intern("object");
    
    rb_hTypes = rb_hash_new();
    rb_define_method(rb_mKernel, "Type", type_new, 1);
    types_hash_init();
    
    rb_mBarracuda = rb_define_module("Barracuda");
    rb_define_const(rb_mBarracuda, "VERSION",  rb_str_new2(VERSION_STRING));
    rb_define_const(rb_mBarracuda, "TYPES", rb_hTypes);
    
    rb_eProgramSyntaxError = rb_define_class_under(rb_mBarracuda, "SyntaxError", rb_eSyntaxError);
    rb_eOpenCLError = rb_define_class_under(rb_mBarracuda, "OpenCLError", rb_eStandardError);
    
    rb_cProgram = rb_define_class_under(rb_mBarracuda, "Program", rb_cObject);
    rb_define_alloc_func(rb_cProgram, program_s_allocate);
    rb_define_method(rb_cProgram, "initialize", program_initialize, -1);
    rb_define_method(rb_cProgram, "compile", program_compile, 1);
    rb_define_method(rb_cProgram, "method_missing", program_method_missing, -1);

    rb_cBuffer = rb_define_class_under(rb_mBarracuda, "Buffer", rb_cObject);
    rb_define_alloc_func(rb_cBuffer, buffer_s_allocate);
    rb_define_method(rb_cBuffer, "initialize", buffer_initialize, -1);
    rb_define_method(rb_cBuffer, "size_changed", buffer_size_changed, 0);
    rb_define_method(rb_cBuffer, "read", buffer_read, 0);
    rb_define_method(rb_cBuffer, "write", buffer_write, 0);
    rb_define_method(rb_cBuffer, "data", buffer_data, 0);
    rb_define_method(rb_cBuffer, "data=", buffer_data_set, 1);

    rb_cOutputBuffer = rb_define_class_under(rb_mBarracuda, "OutputBuffer", rb_cBuffer);
    rb_define_method(rb_cOutputBuffer, "initialize", obuffer_initialize, 2);
    rb_define_method(rb_cOutputBuffer, "size", obuffer_size, 0);
    rb_define_method(rb_cOutputBuffer, "clear", obuffer_clear, 0);
    rb_undef_method(rb_cOutputBuffer, "write");
    rb_undef_method(rb_cOutputBuffer, "size_changed");
    rb_undef_method(rb_cOutputBuffer, "data=");
    
    rb_cType = rb_define_class_under(rb_mBarracuda, "Type", rb_cObject);
    rb_define_method(rb_cType, "initialize", type_initialize, 1);
    rb_define_method(rb_cType, "method_missing", type_method_missing, 1);
    rb_define_method(rb_cType, "object", type_object, 0);
    
    rb_define_method(rb_cObject, "to_type", object_to_type, 1);
    rb_define_method(rb_cObject, "data_type", object_data_type_get, 0);
    rb_define_method(rb_cArray, "data_type", array_data_type_get, 0);
    rb_define_method(rb_cFixnum, "data_type", fixnum_data_type_get, 0);
    rb_define_method(rb_cBignum, "data_type", bignum_data_type_get, 0);
    rb_define_method(rb_cFloat, "data_type", float_data_type_get, 0);
    
    init_opencl();
}