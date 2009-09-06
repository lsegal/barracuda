$:.unshift(File.dirname(__FILE__) + '/../ext/')

require "test/unit"
require "barracuda"

include Barracuda

class TestBuffer < Test::Unit::TestCase
  def test_buffer_create_invalid_data
    assert_raise(TypeError) { Buffer.new("xyz") }
  end
  
  def test_buffer_create_with_array
    b = Buffer.new([1, 2, 3, 4, 5])
    assert_equal [1, 2, 3, 4, 5], b
  end
  
  def test_buffer_create_with_size
    b = Buffer.new(80)
    assert_equal 80, b.size
  end
  
  def test_buffer_mark_dirty
    b = Buffer.new([4, 2, 3])
    b.mark_dirty
    assert b.dirty?
  end
  
  def test_buffer_from_array
    b = Array.new(80).outvar
    assert_kind_of Buffer, b
  end
  
  def test_outvar_buffer
    b = Buffer.new(8)
    assert b.outvar?
  end
end
