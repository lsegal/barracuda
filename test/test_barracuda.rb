$:.unshift(File.dirname(__FILE__) + '/../ext/')

require "test/unit"
require "barracuda"

include Barracuda

class TestBuffer < Test::Unit::TestCase
  def test_buffer_create_no_data
    assert_raise(ArgumentError) { Buffer.new }
  end
  
  def test_buffer_create_invalid_data
    assert_raise(RuntimeError) { Buffer.new("xyz") }
  end
  
  def test_buffer_create_with_array
    b = Buffer.new([1, 2, 3, 4, 5])
    assert_equal [1, 2, 3, 4, 5], b.data
  end
  
  def test_buffer_create_with_splat
    b = Buffer.new(1.0, 2.0, 3.0)
    assert_equal [1.0, 2.0, 3.0], b.data
  end
  
  def test_buffer_set_data
    b = Buffer.new(1)
    b.data = [1, 2, 3]
    assert_equal 3, b.data.size
  end
  
  def test_buffer_read
    b = Buffer.new(4, 2, 3)
    b.data[0] = 1
    b.read
    assert_equal [4,2,3], b.data
  end
  
  def test_buffer_write
    b = Buffer.new(1, 2, 3)
    b.data[0] = 4
    b.write
    b.read
    assert_equal [4,2,3], b.data
  end

  def test_buffer_size_changed
    b = Buffer.new(1, 2, 3)
    b.data << 4
    b.size_changed
    b.read
    assert_equal [1,2,3,4], b.data
  end
end

class TestOutputBuffer < Test::Unit::TestCase
  def test_create_int_output_buffer
    b = OutputBuffer.new(:int, 5)
    assert_equal 5, b.size
  end

  def test_create_int_output_buffer
    b = OutputBuffer.new(:float, 5)
    assert_equal 5, b.size
  end

  def test_create_output_buffer_with_invalid_type
    assert_raise(ArgumentError) { OutputBuffer.new(:char, 5) }
  end
  
  def test_create_output_buffer_with_invalid_size
    assert_raise(ArgumentError) { OutputBuffer.new(:int, 'x') }
  end
end

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
    assert_nothing_raised { p.x_y_z }
  end
  
  def test_kernel_missing
    p = Program.new("__kernel x_y_z(int x) { }")
    assert_raise(NoMethodError) { p.not_x_y_z }
  end
  
  def test_program_int_input_buffer
    p = Program.new <<-'eof'
      __kernel run(__global int* out, __global int* in, int total) {
        int id = get_global_id(0);
        if (id < total) out[id] = in[id] + 1; 
      }
    eof
    
    arr = (1..256).to_a
    _in = Buffer.new(arr)
    out = OutputBuffer.new(:int, arr.size)
    p.run(out, _in, arr.size)
    assert_equal arr.map {|x| x + 1 }, out.data
  end
  
  def test_program_float_buffer
    p = Program.new <<-'eof'
      __kernel run(__global float* out, __global int* in, int total) {
        int id = get_global_id(0);
        if (id < total) out[id] = (float)in[id] + 0.5; 
      }
    eof
    
    arr = (1..256).to_a
    _in = Buffer.new(arr)
    out = OutputBuffer.new(:float, arr.size)
    p.run(out, _in, arr.size)
    assert_equal arr.map {|x| x.to_f + 0.5 }, out.data
  end
  
  def test_program_set_worker_size
    p = Program.new <<-'eof'
      __kernel sum(__global int* out, __global int* in, int total) {
        int id = get_global_id(0);
        if (id < total) atom_add(&out[0], in[id]); 
      }
    eof
    
    arr = (1..517).to_a
    sum = arr.inject(0) {|acc, el| acc + el }
    _in = Buffer.new(arr)
    out = OutputBuffer.new(:int, 1)
    p.sum(out, _in, arr.size, :worker_size => arr.size)
    assert_equal sum, out.data[0]
  end
  
  def test_program_invalid_worker_size
    p = Program.new("__kernel sum(int x) { }")
    assert_raise(ArgumentError) { p.sum(:worker_size => "hello") }
    assert_raise(ArgumentError) { p.sum(:worker => 1) }
  end
  
  def test_program_invalid_args
    p = Program.new("__kernel sum(int x, __global int *y) { }")
    assert_raise(ArgumentError) { p.sum(1, 2) }
    assert_raise(ArgumentError) { p.sum(1, OutputBuffer.new(:int, 1), 3) }
  end
end