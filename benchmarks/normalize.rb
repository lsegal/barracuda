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
    out.push(*vec, 0.0)
  end
  out
end

srand
prog = Program.new <<-'eof'
  __kernel norm(__global float4 *out, __global float4 *in, int total) {
    int i = get_global_id(0);
    if (i < total) out[i] = normalize(in[i]);
  }
eof

num_vecs = 100000
arr = []
num_vecs.times { arr.push(rand, rand, rand, 0.0) }
output = OutputBuffer.new(:float, arr.size)

Benchmark.bmbm do |x|
  x.report("cpu") { norm_all(arr) }
  x.report("gpu") { prog.norm(output, arr, num_vecs) }
end

