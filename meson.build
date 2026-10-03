project('EFVI_ZeroCopy_Matcher', 'cpp',
  version : '2.0.0',
  default_options : ['cpp_std=c++17', 'buildtype=release', 'warning_level=3']
)

threads = dependency('threads')
efvi_inc = include_directories('include')

efvi_matcher = executable(
  'efvi_matcher',
  'src/main.cpp',
  include_directories : efvi_inc,
  dependencies : threads,
)

efvi_hugepage_probe = executable(
  'efvi_hugepage_probe',
  'src/hugepage_probe.cpp',
  include_directories : efvi_inc,
  dependencies : threads,
)

efvi_benchmark = executable(
  'efvi_benchmark',
  'benchmarks/efvi_benchmark.cpp',
  include_directories : efvi_inc,
  dependencies : threads,
)

efvi_allocator_benchmark = executable(
  'efvi_allocator_benchmark',
  'benchmarks/allocator_benchmark.cpp',
  include_directories : efvi_inc,
  dependencies : threads,
)

efvi_tests = executable(
  'efvi_tests',
  'tests/efvi_tests.cpp',
  include_directories : efvi_inc,
  dependencies : threads,
)

test('efvi_tests', efvi_tests)

efvi_order_flow_demo = executable(
  'efvi_order_flow_demo',
  'examples/order_flow_demo.cpp',
  include_directories : efvi_inc,
  dependencies : threads,
)
