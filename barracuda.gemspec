SPEC = Gem::Specification.new do |s|
  s.name = "barracuda"
  s.version = "1.0"
  s.date = "2009-08-30"
  s.author = "Loren Segal"
  s.email = "lsegal@soen.ca"
  s.homepage = "http://github.com/lsegal/barracuda"
  s.summary = "Barracuda is a wrapper library for OpenCL/CUDA GPGPU programming"
  s.files = Dir.glob("{ext,benchmarks,test}/**/*") + ['LICENSE', 'README.md', 'Rakefile'] - ['ext/barracuda.bundle', 'ext/Makefile', 'ext/barracuda.o']
  s.test_files = Dir.glob('test/test_*.rb')
  s.require_paths = ['ext']
  s.extensions    = ['ext/extconf.rb']
  s.rubyforge_project = 'barracuda'
end