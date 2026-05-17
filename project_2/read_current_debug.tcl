connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

puts "BOOT_TRACE"
for {set i 0} {$i < 16} {incr i} {
    set addr [expr {0x012d4dfc + $i * 4}]
    puts [format "BOOT%d=0x%08x" $i [expr {[mrd -value $addr] & 0xffffffff}]]
}
puts "GEM_DEBUG"
for {set i 0} {$i < 8} {incr i} {
    set addr [expr {0x01cca140 + $i * 4}]
    puts [format "ETH%d=0x%08x" $i [expr {[mrd -value $addr] & 0xffffffff}]]
}
puts "KEX_DEBUG"
for {set i 0} {$i < 5} {incr i} {
    set addr [expr {0x011d438c + $i * 4}]
    puts [format "KEX%d=0x%08x" $i [expr {[mrd -value $addr] & 0xffffffff}]]
}
puts "MEM_SYMS"
puts [format "INST_166E8=0x%08x" [expr {[mrd -value 0x010166e8] & 0xffffffff}]]
puts [format "HEAP_START_VAR=0x%08x" [expr {[mrd -value 0x0115d1b0] & 0xffffffff}]]
puts [format "HEAP_SIZE_VAR=0x%08x" [expr {[mrd -value 0x0115d1b4] & 0xffffffff}]]
puts [format "APP_START_VAR=0x%08x" [expr {[mrd -value 0x0115d1b8] & 0xffffffff}]]
puts [format "APP_END_VAR=0x%08x" [expr {[mrd -value 0x0115d1bc] & 0xffffffff}]]
for {set i 0} {$i < 8} {incr i} {
    set addr [expr {0x011d4048 + $i * 4}]
    puts [format "ALLOC_RAW_%d@0x%08x=0x%08x" $i $addr [expr {[mrd -value $addr] & 0xffffffff}]]
}
puts "PAGE_INIT_STACK"
for {set i 0} {$i < 16} {incr i} {
    set addr [expr {0x01cd3fc0 + $i * 4}]
    puts [format "STK_%d@0x%08x=0x%08x" $i $addr [expr {[mrd -value $addr] & 0xffffffff}]]
}
puts [format "ALLOC_PAGES=0x%08x" [expr {[mrd -value 0x011d4050] & 0xffffffff}]]
puts [format "HEAP_TOTAL_PAGES=0x%08x" [expr {[mrd -value 0x011d4054] & 0xffffffff}]]
puts [format "ALLOC_END=0x%08x" [expr {[mrd -value 0x011d4058] & 0xffffffff}]]
puts [format "ALLOC_START=0x%08x" [expr {[mrd -value 0x011d405c] & 0xffffffff}]]

disconnect
