$:.unshift(File.dirname(__FILE__) + '/../ext/')

require "test/unit"
require "barracuda"

include Barracuda

class TestDataTypes < Test::Unit::TestCase
  def test_default_fixnum_type
    assert_equal :int, 2.data_type
  end
  
  def test_default_bignum_type
    assert_equal :long, (2**64).data_type
  end
  
  def test_default_float_type
    assert_equal :float, 2.5.data_type
  end
  
  def test_default_array_type
    assert_equal :int, [2].data_type
    assert_equal :float, [2.5, 2.6].data_type
  end
  
  def test_set_array_type
    assert_equal :uchar, [2].to_type(:uchar).data_type
  end
  
  def test_set_buffer_type
    assert_equal :uchar, Buffer.new([2]).to_type(:uchar).data_type
  end
  
  def test_conversion_to_buffer_maintains_data_type
    o = [2].to_type(:uchar)
    assert_equal :uchar, Buffer.new(o).data_type
  end
  
  def test_set_data_type_fixnum
    assert_equal :char, 2.to_type(:char).data_type
    assert_equal :int, 2.data_type
  end
  
  def test_set_data_type
    [2**64, 2.5, [2]].each do |v|
      assert_equal :char, v.to_type(:char).data_type
    end
  end
  
  def test_set_invalid_data_type
    assert_raise(ArgumentError) { 1.to_type(:unknown) }
  end
  
  def test_invalid_array_data_type
    assert_raise(TypeError) { [Object.new].data_type }
    assert_raise(TypeError) { ['x'].data_type }
    assert_raise(TypeError) { [].data_type }
  end
  
  def test_object_data_type
    assert_nil Object.new.data_type
  end
  
  def test_type_class
    assert_equal :long, Type.new(1).long.data_type
    assert_equal :uchar, Type(1).uchar.data_type
  end
end
