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

Also note that Barracuda currently only supports data types, namely ints and
floats only. This should also be expanded.

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
    
USING
-----

The basic workflow behind the OpenCL architecture is:

1. Create a program (and kernel) to be run on the GPU's many cores.
2. Create input/output buffers to pass data from Ruby to the GPU and back.
3. Read the output buffer(s) to get your computed data.

In Barracuda, this looks basically like:

1. Create a `Barracuda::Program`
2. Create a `Barracuda::Buffer` or `Barracuda::OutputBuffer`
2. Call the kernel method on the program with buffers as arguments
3. Read output buffers

As you can see, there are only 3 basic classes: `Program`, `Buffer` (for input
data), and `OutputBuffer` (for output data).

EXAMPLE
-------

Consider the following example to sum a bunch of integers:

    program = Program.new <<-'eof'
      __kernel sum(__global int *out, __global int *in, int total) {
        int id = get_global_id(0);
        if (id < total) atom_add(&out[0], in[id]); 
      }
    eof
    
    arr    = (1..65536).to_a
    input  = Buffer.new(arr)
    output = OutputBuffer.new(:int, 1)
    program.sum(output, input, arr.size)
    
    puts "The sum is: " + output.data[0].to_s
    
The above example will compute the sum of integers 1 to 65536 using (at most)
65536 parallel processes and return the result in the 1-dimensional output
buffer (which stores integers and is of length 1). The kernel method `sum` 
is called by calling the `#sum` method on the program object, and the 
arguments are passed in sequentially as the output buffer, followed by the
input data (the integers) followed by the total size of the input (since C
does not have the concept of array size).

We can also specify the work group size (the number of iterations we need
to run). Barracuda automatically selects the size of the largest buffer as 
the work group size, but in some cases this may be too small or too large. To
manually specify the work group size, call the kernel with an options hash:

    program.my_kernel_method(..., :worker_size => 512)
    
Note that the work group size must be a power of 2. Barracuda will increase
the work group size to the next power of 2 if it needs to. This means your
OpenCL program might run more iterations of your kernel method than you 
request. Because we can't rely on the work group size, we pass in the total 
data size to ensure we do not exceed the bounds of our data.

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
          - :worker_size => FIXNUM (the number of iterations to run)

**Barracuda::Buffer**:

Stores data to be sent to an OpenCL kernel method
    
    Buffer.new(*buffer_data) => creates a new input buffer
  
    Buffer#data              => accessor for the buffer data
  
    Buffer#size_changed      => call this if the buffer.data was modified and the size changed
      - calls Buffer#write
  
    Buffer#write             => call this if the buffer.data was modified (size not changed)
      - flushes the buffer.data cache to the OpenCL internal memory buffer
  
    Buffer#read              => reads the cached data back into buffer.data
      - refreshes the buffer.data cache according to the internal memory buffer
    
**Barracuda::OutputBuffer**:

Holds a buffer for data written from the kernel method.
    
    OutputBuffer.new(type, size) => creates a new output buffer
      - type can be :float or :int
    
    OutputBufferBuffer#data      => accessor for the buffer data

    OutputBuffer#size            => returns the buffer size

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