require 'mkmf'


# Determine the location of runtime & build libraries
case RUBY_PLATFORM
when /darwin/
  $LDFLAGS += ' -framework OpenCL'
when /linux/
  #locate the OpenCL shared libraries
  libdirs = `ldconfig -p`.lines.grep(/OpenCL/).collect {|l| File.dirname(l.split('=>')[1].strip) }
  libdirs |= libdirs # eliminate duplicates
  raise "OpenCL libraries not found" if libdirs.empty?

  $LDFLAGS += libdirs.collect{|d| " -L#{d}"}.join('')
  
  # search for header files
  hdrdirs = []
  libdirs.each do |dir|
    dir = File.split(dir)
    until dir[0] == "/"
      location = File.join(dir + ['include'])
      if find_header('CL/cl.h', location)
        puts "Found headers: #{location}"
        hdrdirs << location
        break
      end
      dir = File.split(dir[0])
    end
    break unless hdrdirs.empty?
  end
  raise "OpenCL headers not found" if hdrdirs.empty?

when /mingw32/
  # Windows
  raise "Compiling on Windows is not yet supported"
else
  raise "Unrecognized platform"
end


# Determine the availability of the OpenCL shared library
unless find_library('OpenCL', 'clGetDeviceIDs')
  raise "OpenCL library not found"
end

$CFLAGS << " -DRUBY_19" if RUBY_VERSION =~ /1.9/
# Flags for development & debugging
$CFLAGS << ' -g -Wall -Werror -Wno-unused-function'

create_makefile('barracuda')
