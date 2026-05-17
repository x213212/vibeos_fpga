connect
targets -set -filter {name =~ "APU*"}
puts "MEM_PROBE_HEAP_BEGIN"
puts [mrd 0x01c34000 16]
puts "MEM_PROBE_HEAP_BITMAP_END"
puts [mrd 0x01c3bc00 16]
puts "MEM_PROBE_STACK_BEGIN"
puts [mrd 0x0100ffb0 32]
puts "MEM_PROBE_DONE"
exit
