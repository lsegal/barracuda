require 'mkmf'
$CPPFLAGS += " -DRUBY_19" if RUBY_VERSION =~ /1.9/
if RUBY_PLATFORM =~ /darwin/
  $LDFLAGS += " -framework OpenCL" 
else
  $LDFLAGS += " -lOpenCL"
end
create_makefile('barracuda')
