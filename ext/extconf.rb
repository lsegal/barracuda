require 'mkmf'
$CPPFLAGS += " -DRUBY_19" if RUBY_VERSION =~ /1.9/
$LDFLAGS += " -framework OpenCL" if RUBY_PLATFORM =~ /darwin/
create_makefile('barracuda')