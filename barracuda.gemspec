SPEC = Gem::Specification.new do |s|
  s.name = "barracuda"
  s.version = "1.4"
  s.date = "2014-08-31"
  s.author = "Loren Segal"
  s.email = "lsegal@soen.ca"
  s.homepage = "http://github.com/lsegal/barracuda"
  s.summary = "Barracuda is a wrapper library for OpenCL/CUDA GPGPU programming"
  s.files = Dir["{ext,benchmarks,test}/**/*"] + ['LICENSE', 'README.md', 'Rakefile'] - ['ext/barracuda.bundle', 'ext/Makefile', 'ext/barracuda.o']
  s.test_files = Dir['test/test_*.rb']
  s.require_paths = ['ext']
  s.extensions    = ['ext/extconf.rb']
  s.rubyforge_project = 'barracuda'
end
