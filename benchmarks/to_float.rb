$:.unshift(File.dirname(__FILE__) + '/../ext')

require 'barracuda'
require 'benchmark'

include Barracuda

prog = Program.new <<-'eof'
  __kernel sum(__global float *out, __global int *in) {
    int i = get_global_id(0);
    out[i] = ((float)in[i] + 0.5) / 3.8 + 2.0;
  }
eof

arr = (1..333333).to_a
input = Buffer.new(arr)
output = Buffer.new(arr.size).to_type(:float)
 
Benchmark.bmbm do |x|
  x.report("regular") { arr.map {|x| (x.to_f + 0.5) / 3.8 + 2.0 } }
  x.report("opencl") { prog.sum(output, input) }
end

