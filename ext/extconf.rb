require 'mkmf'
$CPPFLAGS += " -DRUBY_19" if RUBY_VERSION =~ /1.9/
hdr = if RUBY_PLATFORM =~ /darwin/
  $LDFLAGS += ' -framework OpenCL'
else
  hdr = 'CL/cl.h'
  unless have_header(hdr)
    if find_header(hdr, '/usr/local/cuda/include')
      # Add library path for cuda
    elsif find_header(hdr, '/opt/AMDAPP/include')
      $LDFLAGS += ' -L/opt/AMDAPP/lib/x86_64/ -L/opt/AMDAPP/lib/x86/'
    else
      puts "Header #{hdr} not found"
      exit(1)
    end
  end
  unless find_library('OpenCL', 'clGetDeviceIDs')
    puts "OpenCL library not found"
    exit(1)
  end
end


create_makefile('barracuda')
