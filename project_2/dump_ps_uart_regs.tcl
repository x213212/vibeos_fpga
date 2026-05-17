connect
if {[catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}]} {
    catch {targets -set -filter {name =~ "APU*"}}
}
puts "REG_DUMP_BEGIN"
foreach addr {
    0xF800012C
    0xF8000154
    0xF8000738
    0xF800073C
    0xE0000000
    0xE0000004
    0xE0000018
    0xE000001C
    0xE000002C
    0xE0000034
    0xE0001000
    0xE0001018
    0xE000102C
    0xE0001034
} {
    puts [format "%s = %s" $addr [mrd -value $addr]]
}
puts "REG_DUMP_END"
disconnect
