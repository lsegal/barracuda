#include <ruby.h>
#include <OpenCL/OpenCL.h>

static VALUE rb_mBarracuda;
static VALUE rb_cBuffer;
static VALUE rb_cOutputBuffer;
static VALUE rb_cProgram;
static VALUE rb_eProgramSyntaxError;
static VALUE rb_eOpenCLError;

static ID ba_worker_size;

static VALUE program_compile(VALUE self, VALUE source);
static VALUE buffer_data_set(VALUE self, VALUE new_value);

static cl_device_id device_id = NULL;
static cl_context context = NULL;
static int err;

#define BUFFER_TYPE_FLOAT 0x0001
#define BUFFER_TYPE_INT   0x0002
#define BUFFER_TYPE_CHAR  0x0003

struct program {
    cl_program program;
};

struct kernel {
    cl_kernel kernel;
};

struct buffer {
    VALUE arr;
    unsigned int type;
    size_t num_items;
    size_t member_size;
    void *cachebuf;
    cl_mem data;
};

#define GET_PROGRAM() \
    struct program *program; \
    Data_Get_Struct(self, struct program, program);
    
#define GET_BUFFER() \
    struct buffer *buffer; \
    Data_Get_Struct(self, struct buffer, buffer);

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
            case BUFFER_TYPE_INT: {
                int value = FIX2INT(item);
                ((int *)buffer->cachebuf)[i] = value;
                break;
            }
            case BUFFER_TYPE_FLOAT: {
                float value = RFLOAT_VALUE(item);
                ((float *)buffer->cachebuf)[i] = value;
                break;
            }
            default:
                ((uint32_t *)buffer->cachebuf)[i] = 0;
        }       
    }
    
    return self;
}

static VALUE
buffer_read(VALUE self)
{
    unsigned int i;
    
    GET_BUFFER();
    
    rb_gc_mark(buffer->arr);
    buffer->arr = rb_ary_new2(buffer->num_items);

    for (i = 0; i < buffer->num_items; i++) {
        switch (buffer->type) {
            case BUFFER_TYPE_INT:
                rb_ary_push(buffer->arr, INT2FIX(((int *)buffer->cachebuf)[i]));
                break;
            case BUFFER_TYPE_FLOAT:
                rb_ary_push(buffer->arr, rb_float_new(((float *)buffer->cachebuf)[i]));
                break;
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

static VALUE
obuffer_initialize(VALUE self, VALUE type, VALUE size)
{
    GET_BUFFER();
    
    StringValue(type);
    if (strcmp(RSTRING_PTR(type), "float") == 0) {
        buffer->type = BUFFER_TYPE_FLOAT;
        buffer->member_size = sizeof(float);
    }
    else if (strcmp(RSTRING_PTR(type), "int") == 0) {
        buffer->type = BUFFER_TYPE_INT;
        buffer->member_size = sizeof(int);
    }
    else {
        rb_raise(rb_eArgError, "type can only be :float or :int");
    }
    
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
        rb_raise(rb_eOpenCLError, "could not execute kernel method '%s'", RSTRING_PTR(argv[0]));
    }

    for (i = 1; i < argc; i++) {
        err = 0;
        if (i == argc - 1 && TYPE(argv[i]) == T_HASH) {
            VALUE worker_size = rb_hash_aref(argv[i], ID2SYM(ba_worker_size));
            if (RTEST(worker_size) && TYPE(worker_size) == T_FIXNUM) {
                global = FIX2UINT(worker_size);
            }
            else {
                CLEAN();
                rb_raise(rb_eArgError, "opts hash must be {:worker_size => INT_VALUE}, got %s",
                    RSTRING_PTR(rb_inspect(argv[i])));
            }
            break;
        }
        
        switch(TYPE(argv[i])) {
            case T_FIXNUM: {
                int value = FIX2INT(argv[i]);
                err = clSetKernelArg(kernel, i - 1, sizeof(int), &value);
                break;
            }
            case T_FLOAT: {
                float value = RFLOAT_VALUE(argv[i]);
                err = clSetKernelArg(kernel, i - 1, sizeof(float), &value);
                break;
            }
            case T_ARRAY: {
                /* TODO */
                /* fall-through */
            }
            default:
                if (CLASS_OF(argv[i]) == rb_cOutputBuffer) {
                    struct buffer *buffer;
                    Data_Get_Struct(argv[i], struct buffer, buffer);
                    err = clSetKernelArg(kernel, i - 1, sizeof(cl_mem), &buffer->data);
                    if (buffer->num_items > global) {
                        global = buffer->num_items;
                    }
                }
                else if (CLASS_OF(argv[i]) == rb_cBuffer) {
                    struct buffer *buffer;
                    Data_Get_Struct(argv[i], struct buffer, buffer);

                    buffer_write(argv[i]);
                    clEnqueueWriteBuffer(commands, buffer->data, CL_TRUE, 0, 
                        buffer->num_items * buffer->member_size, buffer->cachebuf, 0, NULL, NULL);
                    err = clSetKernelArg(kernel, i - 1, sizeof(cl_mem), &buffer->data);
                    if (buffer->num_items > global) {
                        global = buffer->num_items;
                    }
                }
                break;
        }
        if (err != CL_SUCCESS) {
            CLEAN();
            rb_raise(rb_eArgError, "invalid kernel method parameter: %s", RSTRING_PTR(rb_inspect(argv[i])));
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
        if (CLASS_OF(argv[i]) == rb_cOutputBuffer) {
            struct buffer *buffer;
            Data_Get_Struct(argv[i], struct buffer, buffer);
            err = clEnqueueReadBuffer(commands, buffer->data, CL_TRUE, 0, 
                buffer->num_items * buffer->member_size, buffer->cachebuf, 0, NULL, NULL);
            ERROR("failed to read output buffer");
            buffer_read(argv[i]);
        }
    }

    CLEAN();
    return Qnil;
}

void
Init_barracuda()
{
    ba_worker_size = rb_intern("worker_size");
    
    rb_mBarracuda = rb_define_module("Barracuda");
    
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
    rb_undef_method(rb_cOutputBuffer, "write");
    rb_undef_method(rb_cOutputBuffer, "size_changed");
    rb_undef_method(rb_cOutputBuffer, "data=");

    init_opencl();
}