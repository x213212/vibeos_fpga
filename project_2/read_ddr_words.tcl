set out [open "H:/testproject/project_2/read_ddr_words.out" "w"]
connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
puts $out "READ_DDR_WORDS_BEGIN"
foreach addr {0x01000000 0x01000020 0x01000034 0x01000040 0x01002328 0x012d4dc4} {
    puts $out [format "ADDR=0x%08x" $addr]
    catch {puts $out [mrd $addr 8]}
}
close $out
catch {con}
disconnect
