$:.unshift(File.dirname(__FILE__) + '/../ext')

require 'barracuda'
require 'benchmark'

include Barracuda

def dist(*vec)
  vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2]
end

def normalize(*vec)
  d = dist(*vec)
  vec.map {|c| c / d }
end

def norm_all(arr)
  out = []
  0.step(arr.size - 1, 4) do |i|
    vec = normalize(arr[i], arr[i + 1], arr[i + 2])
    out.push(vec, 0.0)
  end
  out.flatten
end

srand
prog = Program.new <<-'eof'
  __kernel void norm(__global float4 *out, __global float4 *in, int total) {
    int i = get_global_id(0);
    if (i < total) out[i] = normalize(in[i]);
  }
eof

num_vecs = 1000000
arr = []
num_vecs.times { arr.push(rand, rand, rand, 0.0) }
output = Buffer.new(arr.size).to_type(:float)

Benchmark.bmbm do |x|
  # As done above in #norm_all
  x.report("ruby") { norm_all(arr) }
  # As done above in Kernel
  x.report("opencl") { prog.norm(output, arr, num_vecs) }
end

