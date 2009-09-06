$:.unshift(File.dirname(__FILE__) + '/../ext/')

require "test/unit"
require "barracuda"

include Barracuda

class TestProgram < Test::Unit::TestCase
  def test_program_create_invalid_code
    assert_raise(Barracuda::SyntaxError) { Program.new "fib { SYNTAXERROR }" }
  end
  
  def test_program_create
    assert_nothing_raised { Program.new "__kernel fib(int x) { return 0; }"}
  end
  
  def test_program_compile
    p = Program.new
    assert_nothing_raised { p.compile "__kernel fib(int x) { }" }
  end
  
  def test_kernel_run
    p = Program.new("__kernel x_y_z(int x) { }")
    assert_raise(ArgumentError) { p.x_y_z }
  end
  
  def test_kernel_missing
    p = Program.new("__kernel x_y_z(int x) { }")
    assert_raise(NoMethodError) { p.not_x_y_z }
  end
  
  def test_program_implicit_array_buffer
    p = Program.new <<-'eof'
      __kernel copy(__global int *out, __global int *in) {
        int i = get_global_id(0);
        out[i] = in[i] + 1;
      }
    eof
    
    out = Buffer.new(3)
    p.copy(out, [1, 2, 3])
    assert_equal [2, 3, 4], out
  end
  
  def test_program_types
    arr = (1..5).to_a
    outarr = arr.map {|x| x + 1 }
    p = Program.new
  
    TYPES.keys.each do |type|
      # FIXME These types are currently broken (unimplemented in opencl?)
      next if type == :bool
      next if type == :double
      next if type == :size_t
      next if type == :ptrdiff_t
      next if type == :intptr_t
      next if type == :uintptr_t
  
      p.compile <<-eof
        __kernel run(__global #{type} *out, __global #{type} *in) {
          int id = get_global_id(0);
          out[id] = in[id] + 1;
        }
      eof
    
      out = Buffer.new(arr.size).to_type(type)
      p.run(out, arr.to_type(type))
      assert_equal({type => outarr}, {type => out})
    end
  end
  
  def test_program_int_input_buffer
    p = Program.new <<-'eof'
      __kernel run(__global int* out, __global int* in) {
        int id = get_global_id(0);
        out[id] = in[id] + 1; 
      }
    eof
    
    input = (1..256).to_a
    out = Buffer.new(input.size).to_type(:int)
    p.run(out, input)
    assert_equal input.map {|x| x + 1 }, out
  end
  
  def test_program_float_buffer
    p = Program.new <<-'eof'
      __kernel run(__global float* out, __global int* in) {
        int id = get_global_id(0);
        out[id] = (float)in[id] + 0.5; 
      }
    eof
    
    input = (1..256).to_a
    out = Buffer.new(input.size).to_type(:float)
    p.run(out, input)
    assert_equal input.map {|x| x.to_f + 0.5 }, out
  end
  
  def test_program_set_times
    p = Program.new <<-'eof'
      __kernel sum(__global int* out, __global int* in) {
        int id = get_global_id(0);
        atom_add(out, in[id]); 
      }
    eof
    
    input = (1..517).to_a
    sum = input.inject(0) {|acc, el| acc + el }
    out = Buffer.new(1)
    p.sum(out, input, :times => input.size)
    assert_equal sum, out[0]
  end
  
  def test_program_largest_buffer_is_input
    p = Program.new <<-'eof'
      __kernel sum(__global int* out, __global int* in) {
        int id = get_global_id(0);
        atom_add(out, in[id]); 
      }
    eof
    
    input = (1..517).to_a
    sum = input.inject(0) {|acc, el| acc + el }
    out = Buffer.new(1)
    p.sum(out, input)
    assert_equal sum, out[0]
  end
  
  def test_program_invalid_times
    p = Program.new("__kernel sum(int x) { }")
    assert_raise(ArgumentError) { p.sum(:times => "hello") }
    assert_raise(ArgumentError) { p.sum(:time => 1) }
  end
  
  def test_program_invalid_args
    p = Program.new("__kernel sum(int x, __global int *y) { }")
    assert_raise(ArgumentError) { p.sum(1, 2) }
    assert_raise(ArgumentError) { p.sum(1, Buffer.new(1), 3) }
  end
  
  def test_program_vectors
    p = Program.new <<-'eof'
      __kernel copy_to_out(__global float4 *out, __global float4 *vec) {
        out[0].x = vec[0].x + 0.5;
        out[0].y = vec[0].y + 0.5;
        out[0].z = vec[0].z + 0.5;
        out[0].w = vec[0].w + 0.5;
      }
    eof
    
    out = Buffer.new(4).to_type(:float)
    p.copy_to_out(out, [2.5, 2.5, 2.5, 2.5])
    assert_equal [3, 3, 3, 3], out
  end
  
  def test_program_no_total
    p = Program.new <<-'eof'
      __kernel copy(__global int *out, __global int *in) {
        int i = get_global_id(0);
        out[i] = in[i] + 1;
      }
    eof
    
    out = Buffer.new(3)
    p.copy(out, (1..3).to_a)
    assert_equal (2..4).to_a, out
  
    out = Buffer.new(50446)
    p.copy(out, (1..50446).to_a)
    assert_equal (2..50447).to_a, out
  end
end