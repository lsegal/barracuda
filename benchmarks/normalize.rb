$:.unshift(File.dirname(__FILE__) + '/../ext')

require 'barracuda'
require 'benchmark'

include Barracuda

def dist(vec)
  Math.sqrt(vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2] + vec[3] * vec[3])
end

def normalize(vec)
  d = dist(vec)
  vec.map {|c| c / d }
end

def norm_all(arr)
  out = []
  0.step(arr.size - 3, 4) do |i|
    vec = normalize(arr[i...(i + 4)])
    out.push(*vec)
  end
  out
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
num_vecs.times { arr.push(rand, rand, rand, rand) }
output = Buffer.new(arr.size).to_type(:float)

Benchmark.bmbm do |x|
  # As done above in #norm_all
  x.report("ruby") { @result1 = norm_all(arr) }
  # As done above in Kernel
  x.report("opencl") { @result2 = prog.norm(output, arr, num_vecs) }
end

if @result1.length != @result2.length
  STDERR.puts "Invalid result"
  exit(1)
end

error = @result1.size.times.find { |i| (@result1[i] - @result2[i]).abs > 0.000001 }
if error
  STDERR.puts "Error: #{@result1[error]} != #{@result2[error]}"
  exit(1)
end

