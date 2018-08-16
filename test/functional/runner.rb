#!/usr/bin/ruby

test_files = Dir.glob('test/functional/*.py')
test_files.delete 'test/functional/combine_logs.py'
test_files.delete 'test/functional/test_runner.py'

passed = []
failed = []
nownow = Time.new

test_files.each do |file|
  puts "Running #{file}..."
  start = Time.new
  ok = system(file)
  if ok
    puts "#{file} Passed! (took #{Time.new - start} secs)"
    passed << file
  else
    puts "#{file} Failed! (took #{Time.new - start} secs)"
    failed << file
  end
  puts
end

puts
puts "Stats and Info:"
puts "---------------"
puts "Tests:  #{test_files.size}"
puts "Passed: #{passed.size}"
puts "Failed: #{failed.size}"
totsecs = Time.new - nownow
puts "(took #{totsecs} secs total (#{totsecs/60} min)"
unless failed.empty?
puts
puts "Failures:"
  failed.each do |file|
    puts file
  end
end