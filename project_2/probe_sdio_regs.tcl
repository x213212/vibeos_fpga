connect -url tcp:127.0.0.1:3121
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}

proc rd {name addr} {
    puts [format "%s=0x%08x" $name [mrd -value $addr]]
}

rd SDIO0_00 0xE0100000
rd SDIO0_04 0xE0100004
rd SDIO0_24 0xE0100024
rd SDIO0_28 0xE0100028
rd SDIO0_2C 0xE010002C
rd SDIO0_30 0xE0100030
rd SDIO1_00 0xE0101000
rd SDIO1_04 0xE0101004
rd SDIO1_24 0xE0101024
rd SDIO1_28 0xE0101028

disconnect
