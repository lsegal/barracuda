Barracuda
=========

Written by Loren Segal in 2009.

SYNOPSIS
--------

Barracuda is a Ruby wrapper library for the [OpenCL][1] architecture. OpenCL is a
framework for multi-processor computing, most notably allowing a programmer
to run parallel programs on a GPU, taking advantage of the many cores
available.

Barracuda aims to abstract both CUDA and OpenCL, however for now only OpenCL
on OSX 10.6 is supported. Patches to extend this support would be joyously
accepted!

INSTALLING
----------

As mentioned above, this library currently only supports OSX 10.6 (or an earlier
version with the OpenCL framework, if that's even possible). If you manage to
mess with the source and get it working on [insert system here], please submit
your patches.

Okay, assuming you have a compatible machine:

    sudo gem install barracuda
    
Or:

    git clone git://github.com/lsegal/barracuda
    cd barracuda
    rake install
    
USAGE
-----

The basic workflow behind the OpenCL architecture is:

1. Create a program (and kernel) to be run on the GPU's many cores.
2. Create input/output buffers to pass data from Ruby to the GPU and back.
3. Read the output buffer(s) to get your computed data.

In Barracuda, this looks basically like:

1. Create a `Barracuda::Program`
2. Create a `Barracuda::Buffer` for input and output
2. Call the kernel method on the program with buffers as arguments
3. Read output buffers

As you can see, there are only 2 basic classes: `Program` and `Buffer`. The
program is where you compile your OpenCL code, and the Buffer class is a 
subclass of Array that contains your data to pass in and out of the kernel
method.

EXAMPLE
-------

Consider the following example to sum a bunch of integers:

    program = Program.new <<-'eof'
      __kernel sum(__global int *in, __global int *out) {
        atom_add(out, in[get_global_id(0)]); 
      }
    eof
    
    output = Buffer.new(1)
    program.sum((1..65536).to_a, output)
    
    puts "The sum is: " + output.data[0].to_s
    
The above example will compute the sum of integers 1 to 65536 using (at most)
65536 parallel processes and return the result in the 1-dimensional output
buffer (which stores integers and is of length 1). The kernel method `sum` 
is called by calling the `#sum` method on the program object, and the 
arguments are passed in sequentially as the input data (the integers) 
followed by the output buffer to store the data.

We can also specify the work group size (the number of iterations we need
to run). Barracuda automatically selects the size of the largest buffer as 
the work group size, but in some cases this may be too small or too large. To
manually specify the work group size, call the kernel with an options hash:

    program.my_kernel_method(..., :times => 512)
    
OUTPUT BUFFERS
--------------

The Buffer class is a superset of both data to be sent and read from the OpenCL
kernel method being called. In general, if the Buffer contains nil elements,
it is marked as an "output buffer" and the data is read back from OpenCL after
the kernel method executes. These nil buffers are not written to OpenCL initially,
so they are only meant for output data. On the other hand, if the buffer contains
regular data, it is by default considered as input data only, and the data
is not read back after the kernel method completes.

In some cases you may want to have a buffer that is both input and output and
should be read from after the kernel method finishes. To do this, you mark the
buffer as an `outvar` as so:

    program = Program.new <<-'eof'
      __kernel addN(__global int *data, int N) {
        int i = get_global_id(0);
        data[i] = data[i] + N;
      }
    eof
    
    data = [1, 2, 3]
    program.addN(data.outvar, 10) 
    
    # prints: [11, 12, 13]
    p data 

RETURN VALUE
------------

Generally you need to pass in your output buffer as the buffer to write the
data back to. The idiom `void method(input, output)` is common to write data to
output buffers in languages such as C but is a rather clunky API for Ruby.
Instead, Barracuda returns the output buffers as the result of the kernel method 
call. If there is only one output buffer, that buffer is returned as a single 
result (rather than an array of buffers). 

The example above could be simply rewritten as:

    # prints: [11, 12, 13]
    p program.addN(data.outvar, 10)

CONVERTING TYPES
----------------

OpenCL has a variety of native types. Most of them are supported, however some
are not. Because Ruby only has the concept of Float and Fixnum (integer), you
may need to tell Barracuda the type of your input if you're trying to pass in
a char, short or double (or possibly have some signedness restrictions). To
do this, simply call `.to_type(:my_type)` on the input where `:my_type` is
a key in the `Barracuda::TYPES` hash:

    >> Barracuda::TYPES.keys
    => [:bool, :char, :uchar, :short, :ushort, :int, :uint, :long, 
        :ulong, :float, :half, :double, :size_t, :ptrdiff_t, 
        :intptr_t, :uintptr_t]

For example, to pass in a short, do:

    program.my_kernel(2.to_type(:short))
    
This can also be applied to an Array of shorts:

    program.my_kernel([1, 2, 3].to_type(:short))
    
The default type for an array (and buffers) is :int

CLASS DETAILS
-------------

**Barracuda::Program**:

Represents an OpenCL program
    
    Program.new(PROGRAM_SOURCE)  => creates a new program

    Program#compile(SOURCE)      => recompiles a program

    Program#KERNEL_METHOD(*args) => runs KERNEL_METHOD in the compiled program
      - args should be the arguments defined in the kernel method.
      - supported argument types are Float and Fixnum objects only.
      - if the last arg is a Hash, it should be an options hash with keys:
          - :times => FIXNUM (the number of iterations to run)

**Barracuda::Buffer** (extends *Array*):

Data storage to transfer to/from an OpenCL kernel method
    
    Buffer.new(buffer_array) => creates a new input buffer
    Buffer.new(size)         => creates a new output buffer of size `size`
  
    Buffer#mark_dirty        => call this if the data was modified between calls

    Buffer#dirty?            => returns whether the buffer is marked as dirty
    
    Buffer#outvar            => mark the buffer to be read as output
    
    Buffer#outvar?           => returns whether buffer is marked to be read
    
GLOSSARY
--------

* **Program**: an OpenCL program is generally created from a variant of C that
  has extra domain specific keywords. A program has at least one "kernel" 
  method, but can have many regular methods.

* **Kernel**: a special "entry" method in the program that is exposed to the 
  programmer to be called on via the OpenCL framework. A kernel method is 
  represented by the `__kernel` keyword before the method body.

* **Buffer**: memory storage which is accessible and (generally shared with the 
  program). Buffers are usually marked with the `__global` keyword in an 
  OpenCL program.

COPYRIGHT & LICENSING
---------------------

Copyright 2009 Loren Segal, licensed under the MIT License

[1]: http://en.wikipedia.ca/wiki/OpenCL "OpenCL"