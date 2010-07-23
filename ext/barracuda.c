#include <ruby.h>
#include <math.h>
#ifdef SYS_MACOSX
#    include <OpenCL/opencl.h>
#else
#    include <CL/cl.h>
#endif


#ifndef RFLOAT_VALUE
#   define RFLOAT_VALUE(v) (RFLOAT(v)->value)
#endif

static VALUE rb_mBarracuda;
static VALUE rb_cBuffer;
static VALUE rb_cProgram;
static VALUE rb_eProgramSyntaxError;
static VALUE rb_eOpenCLError;
static VALUE rb_cType;
static VALUE rb_hTypes;

static ID id_times;
static ID id_to_s;
static ID id_new;
static ID id_object;
static ID id_data_type;
static ID id_buffer_data;

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
static ID id_type_double;
static ID id_type_size_t;
static ID id_type_ptrdiff_t;
static ID id_type_intptr_t;
static ID id_type_uintptr_t;
/*static ID id_type_void;*/

static VALUE program_compile(VALUE self, VALUE source);

static cl_platform_id platform_id = NULL;
static cl_device_id device_id = NULL;
static cl_context context = NULL;
static size_t max_work_group_size = 65535;
static int err;

#define VERSION_STRING "1.3"

struct program {
    cl_program program;
};

struct buffer {
    VALUE dirty;
    VALUE outvar;
    ID type;
    size_t member_size;
    size_t num_items;
    int8_t *cachebuf;
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
    if (RTEST(value)) return value;
    
    if (RARRAY_LEN(self) > 0) {
        if (NIL_P(RARRAY_PTR(self)[0])) return ID2SYM(id_type_int);
        VALUE value = rb_funcall(RARRAY_PTR(self)[0], id_data_type, 0);
        if (RTEST(value)) return value;
    }
    
    rb_raise(rb_eTypeError, "unknown buffer data %s", 
        RSTRING_PTR(rb_inspect(self)));
}

#define GET_PROGRAM() \
    struct program *program; \
    Data_Get_Struct(self, struct program, program);
    
#define GET_BUFFER() \
    struct buffer *buffer; \
    Data_Get_Struct(rb_ivar_get(self, id_buffer_data), struct buffer, buffer);

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
    TYPE_SET(double,    cl_float);
    TYPE_SET(size_t,    cl_uint);
    TYPE_SET(ptrdiff_t, cl_uint);
    TYPE_SET(intptr_t,  cl_uint);
    TYPE_SET(uintptr_t, cl_uint);
    OBJ_FREEZE(rb_hTypes);
}

static void
type_to_native(VALUE value, ID data_type, void *native_value)
{
    if (id_type_char == data_type || id_type_uchar == data_type) {
        if (TYPE(value) == T_FIXNUM) {
            value = rb_funcall(value, rb_intern("chr"), 0);
        }
        *((cl_char *)native_value) = RSTRING_PTR(value)[0];
        return;
    }
    if (id_type_float == data_type || id_type_double == data_type) {
        *((cl_float *)native_value) = TYPE(value) == T_FIXNUM ? 
            (cl_float)FIX2INT(value) : RFLOAT_VALUE(value);
        return;
    }
    if (id_type_half == data_type) {
        *((cl_half *)native_value) = TYPE(value) == T_FIXNUM ? 
            (cl_half)FIX2INT(value) : RFLOAT_VALUE(value);
        return;
    }
    
    TYPE_TO_NATIVE(bool,      char,       FIX2INT);
    TYPE_TO_NATIVE(short,     cl_short,   FIX2INT);
    TYPE_TO_NATIVE(ushort,    cl_ushort,  NUM2UINT);
    TYPE_TO_NATIVE(int,       cl_int,     FIX2INT);
    TYPE_TO_NATIVE(uint,      cl_uint,    NUM2UINT);
    TYPE_TO_NATIVE(long,      cl_long,    NUM2LONG);
    TYPE_TO_NATIVE(ulong,     cl_ulong,   NUM2ULONG);
    TYPE_TO_NATIVE(size_t,    cl_uint,    NUM2UINT);
    TYPE_TO_NATIVE(ptrdiff_t, cl_uint,    NUM2UINT);
    TYPE_TO_NATIVE(intptr_t,  cl_uint,    NUM2UINT);
    TYPE_TO_NATIVE(uintptr_t, cl_uint,    NUM2UINT);
}

static VALUE
type_to_ruby(void *native_value, ID data_type)
{
    TYPE_TO_RUBY(bool,      char,       INT2FIX);
    TYPE_TO_RUBY(char,      cl_char,    INT2FIX);
    TYPE_TO_RUBY(uchar,     cl_uchar,   UINT2NUM);
    TYPE_TO_RUBY(short,     cl_short,   INT2FIX);
    TYPE_TO_RUBY(ushort,    cl_ushort,  UINT2NUM);
    TYPE_TO_RUBY(int,       cl_int,     INT2FIX);
    TYPE_TO_RUBY(uint,      cl_uint,    UINT2NUM);
    TYPE_TO_RUBY(long,      cl_long,    LONG2NUM);
    TYPE_TO_RUBY(ulong,     cl_ulong,   ULONG2NUM);
    TYPE_TO_RUBY(float,     cl_float,   rb_float_new);
    TYPE_TO_RUBY(half,      cl_half,    rb_float_new);
    TYPE_TO_RUBY(double,    cl_float,   rb_float_new);
    TYPE_TO_RUBY(size_t,    cl_uint,    UINT2NUM);
    TYPE_TO_RUBY(ptrdiff_t, cl_uint,    UINT2NUM);
    TYPE_TO_RUBY(intptr_t,  cl_uint,    UINT2NUM);
    TYPE_TO_RUBY(uintptr_t, cl_uint,    UINT2NUM);
    return Qnil;
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
    rb_ivar_set(self, id_data_type, type);
    return self;
}

static VALUE
fixnum_to_type(VALUE self, VALUE type)
{
    VALUE out = rb_funcall(rb_cType, rb_intern("new"), 1, self);
    return type_method_missing(out, type);
}

static VALUE
type_new(VALUE klass, VALUE type)
{
    return rb_funcall(rb_cType, id_new, 1, type);
}

static void
free_buffer_data(struct buffer *buffer)
{
    clReleaseMemObject(buffer->data);
    ruby_xfree(buffer->cachebuf);
}

static VALUE
buffer_outvar(VALUE self)
{
    GET_BUFFER();
    buffer->outvar = Qtrue;
    return self;
}

static VALUE
buffer_is_outvar(VALUE self)
{
    GET_BUFFER();
    return buffer->outvar;
}

static VALUE
buffer_dirty(VALUE self)
{
    GET_BUFFER();
    if (buffer->dirty == Qtrue) return Qtrue;
    if (buffer->data == NULL) return Qtrue;
    if (buffer->cachebuf == NULL) return Qtrue;
    if (RARRAY_LEN(self) != buffer->num_items) return Qtrue;
    if (SYM2ID(rb_funcall(self, id_data_type, 0)) != buffer->type) return Qtrue;
    return Qfalse;
}

static VALUE
buffer_mark_dirty(VALUE self)
{
    GET_BUFFER();
    return (buffer->dirty = Qtrue);
}

static void
buffer_size_changed(struct buffer *buffer)
{
    clReleaseMemObject(buffer->data);
    buffer->data = clCreateBuffer(context, CL_MEM_READ_WRITE, 
            buffer->num_items * buffer->member_size, NULL, NULL);
    ruby_xfree(buffer->cachebuf);
    buffer->cachebuf = ruby_xmalloc(buffer->num_items * buffer->member_size);
}

static VALUE
buffer_update_cache(VALUE self)
{
    GET_BUFFER();

    if (buffer_dirty(self) == Qtrue) {
        size_t old_num_items = buffer->num_items;
        buffer->num_items = RARRAY_LEN(self);
        buffer->type = SYM2ID(rb_funcall(self, id_data_type, 0));
        buffer->member_size = FIX2INT(rb_hash_aref(rb_hTypes, ID2SYM(buffer->type)));
        if (buffer->num_items != old_num_items) buffer_size_changed(buffer);
        buffer->dirty = Qfalse;
        return Qtrue;
    }
    
    return Qnil;
}

static void
print_buffer(struct buffer *buffer)
{
    int i;
    for (i = 0; i < buffer->num_items * buffer->member_size; i++) {
        int c = (int)buffer->cachebuf[i];
        if (i > 0 && i % 8 == 0) printf("\n");
        printf("%2.2x ", c);
    }
    printf("\n");
    fflush(stdout);
}

static VALUE
buffer_write(VALUE self, cl_command_queue queue)
{
    unsigned int i, index;
    unsigned long data_ptr[16]; // data buffer
    
    GET_BUFFER();

    if (NIL_P(RARRAY_PTR(self)[0])) return Qnil;
    
    for (i = 0, index = 0; i < buffer->num_items; i++, index += buffer->member_size) {
        VALUE item = RARRAY_PTR(self)[i];
        type_to_native(item, buffer->type, data_ptr);
        memcpy(buffer->cachebuf + index, data_ptr, buffer->member_size);
    }
    
    if (queue != NULL) {
        clEnqueueWriteBuffer(queue, buffer->data, CL_TRUE, 0, 
            buffer->num_items * buffer->member_size, buffer->cachebuf, 0, NULL, NULL);
    }
    
    return self;
}

static VALUE
buffer_read(VALUE self, cl_command_queue queue)
{
    unsigned int i, index;
    
    GET_BUFFER();
    
    if (buffer->outvar != Qtrue) return Qnil;

    if (queue != NULL) {
        clEnqueueReadBuffer(queue, buffer->data, CL_TRUE, 0, 
            buffer->num_items * buffer->member_size, buffer->cachebuf, 0, NULL, NULL);
    }
    
    for (i = 0, index = 0; i < buffer->num_items; i++, index += buffer->member_size) {
        VALUE value = type_to_ruby(buffer->cachebuf + index, buffer->type);
        rb_ary_store(self, i, value);
    }
    
    return self;
}

static VALUE
array_to_outvar(VALUE self)
{
    VALUE buf = rb_funcall(rb_cBuffer, id_new, 0);
    rb_funcall(buf, rb_intern("replace"), 1, self);
    buffer_outvar(buf);
    buffer_mark_dirty(buf);
    return buf;
}

static VALUE
buffer_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE buf_value;
    struct buffer *buffer;
    
    rb_call_super(argc, argv);
    
    if (argc == 1 && TYPE(argv[0]) == T_ARRAY) {
        VALUE value = rb_ivar_get(argv[0], id_data_type);
        if (RTEST(value)) rb_ivar_set(self, id_data_type, value);
    }

    buffer = ALLOC(struct buffer);
    MEMZERO(buffer, struct buffer, 1);
    buffer->outvar = Qfalse;
    buffer->dirty = Qtrue;
    buf_value = Data_Wrap_Struct(rb_cObject, 0, free_buffer_data, buffer);
    rb_ivar_set(self, id_buffer_data, buf_value);

    if (RARRAY_LEN(self) > 0 && NIL_P(RARRAY_PTR(self)[0])) { /* outvar */
        buffer->outvar = Qtrue;
    }
    
    return self;
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

#define CLEAN() { clReleaseKernel(kernel); clReleaseCommandQueue(commands); }

static VALUE
program_method_missing(int argc, VALUE *argv, VALUE self)
{
    int i;
    size_t global[3] = {1, 1, 1}, local[3] = {0, 1, 1}, tmp;
    cl_kernel kernel;
    cl_command_queue commands;
    VALUE result;
    GET_PROGRAM();
    
    argv[0] = rb_funcall(argv[0], id_to_s, 0);
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
            VALUE worker_size = rb_hash_aref(item, ID2SYM(id_times));
            if (RTEST(worker_size) && TYPE(worker_size) == T_FIXNUM) {
                global[0] = FIX2UINT(worker_size);
            }
            else {
                CLEAN();
                rb_raise(rb_eArgError, "opts hash must be {:times => INT_VALUE}, got %s",
                    RSTRING_PTR(rb_inspect(item)));
            }
            break;
        }
        
        if (CLASS_OF(item) == rb_cArray) {
            /* create buffer from arg */
            argv[i] = item = rb_funcall(rb_cBuffer, id_new, 1, item);
        }

        if (CLASS_OF(item) == rb_cBuffer) {
            struct buffer *buffer;
            Data_Get_Struct(rb_ivar_get(item, id_buffer_data), struct buffer, buffer);
            
            buffer_update_cache(item);
            buffer_write(item, commands);
            err = clSetKernelArg(kernel, i - 1, sizeof(cl_mem), &buffer->data);
            if (RARRAY_LEN(item) > global[0]) {
                global[0] = RARRAY_LEN(item);
            }
        }
        else {
            unsigned long data_ptr[16]; // a buffer of data
            size_t data_size_t;
            VALUE data_type, data_size;
            
            if (CLASS_OF(item) == rb_cType) {
                data_type = rb_funcall(item, id_data_type, 0);
                item = type_object(item);
            }
            else {
                data_type = rb_funcall(item, id_data_type, 0);
            }
            data_size = rb_hash_aref(rb_hTypes, data_type);
            if (NIL_P(data_size)) {
                CLEAN();
                rb_raise(rb_eTypeError, "invalid data type for %s", 
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
    
    err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &tmp, NULL);
    err = clEnqueueNDRangeKernel(commands, kernel, 3, NULL, global, local[0] == 0 ? NULL : local, 0, NULL, NULL);
    if (err != CL_SUCCESS) { 
        CLEAN(); 
        if (err == CL_INVALID_KERNEL_ARGS) {
            rb_raise(rb_eArgError, "invalid arguments"); 
        }
        else {
            rb_raise(rb_eOpenCLError, "failed to execute kernel method %d", err); 
        }
    }
    
    clFinish(commands);
    
    result = rb_ary_new();

    for (i = 1; i < argc; i++) {
        VALUE item = argv[i];
        if (CLASS_OF(item) == rb_cBuffer) {
            if (RTEST(buffer_read(item, commands))) {
                rb_ary_push(result, item);
            }
        }
    }

    CLEAN();
    
    if (RARRAY_LEN(result) == 0) {
        return Qnil;
    }
    else if (RARRAY_LEN(result) == 1) {
        return RARRAY_PTR(result)[0];
    }
    else {
        return result;
    }
}

static void
init_opencl()
{
    if (platform_id == NULL) {
        // TODO: Get all the platforms
        err = clGetPlatformIDs(1, &platform_id, NULL);
        if (err != CL_SUCCESS) {
            rb_raise(rb_eOpenCLError, "failed to create a platform group.");
        }
    }

    if (device_id == NULL) {
        // TODO: Get all the devices
        err = clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_ALL, 1, &device_id, NULL);
        if (err != CL_SUCCESS) {
            rb_raise(rb_eOpenCLError, "failed to create a device group.");
        }
    }

    if (context == NULL) {
        // TODO: Make a context spanning all the devices
        context = clCreateContext(0, 1, &device_id, NULL, NULL, &err);
        if (!context) {
            rb_raise(rb_eOpenCLError, "failed to create a program context");
        }
    }
    
    clGetDeviceInfo(device_id, CL_DEVICE_MAX_WORK_GROUP_SIZE, 
        sizeof(size_t), &max_work_group_size, NULL);
    max_work_group_size = 4096;
}

void
Init_barracuda()
{
    id_times = rb_intern("times");
    id_new = rb_intern("new");
    id_to_s = rb_intern("to_s");
    id_data_type = rb_intern("data_type");
    id_buffer_data = rb_intern("buffer_data");
    
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

    rb_cBuffer = rb_define_class_under(rb_mBarracuda, "Buffer", rb_cArray);
    rb_define_method(rb_cBuffer, "initialize", buffer_initialize, -1);
    rb_define_method(rb_cBuffer, "outvar", buffer_outvar, 0);
    rb_define_method(rb_cBuffer, "outvar?", buffer_is_outvar, 0);
    rb_define_method(rb_cBuffer, "mark_dirty", buffer_mark_dirty, 0);
    rb_define_method(rb_cBuffer, "dirty?", buffer_dirty, 0);
    
    rb_cType = rb_define_class_under(rb_mBarracuda, "Type", rb_cObject);
    rb_define_method(rb_cType, "initialize", type_initialize, 1);
    rb_define_method(rb_cType, "method_missing", type_method_missing, 1);
    rb_define_method(rb_cType, "object", type_object, 0);
    
    rb_define_method(rb_cArray, "outvar", array_to_outvar, 0);
    rb_define_method(rb_cObject, "to_type", object_to_type, 1);
    rb_define_method(rb_cFixnum, "to_type", fixnum_to_type, 1);
    rb_define_method(rb_cObject, "data_type", object_data_type_get, 0);
    rb_define_method(rb_cArray, "data_type", array_data_type_get, 0);
    rb_define_method(rb_cFixnum, "data_type", fixnum_data_type_get, 0);
    rb_define_method(rb_cBignum, "data_type", bignum_data_type_get, 0);
    rb_define_method(rb_cFloat, "data_type", float_data_type_get, 0);
    
    init_opencl();
}
