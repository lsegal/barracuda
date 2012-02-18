require 'mkmf'
$CPPFLAGS += " -DRUBY_19" if RUBY_VERSION =~ /1.9/
hdr = if RUBY_PLATFORM =~ /darwin/
  $LDFLAGS += ' -framework OpenCL'
else
  hdr = 'CL/cl.h'
  unless have_header(hdr)
    unless find_header(hdr, '/usr/local/cuda/include') or find_header(hdr, '/opt/AMDAPP/include')
      puts "Header #{hdr} not found"
      exit(1)
    end
  end
  unless have_library('OpenCL')
    puts "OpenCL library not found"
    exit(1)
  end
end


create_makefile('barracuda')
