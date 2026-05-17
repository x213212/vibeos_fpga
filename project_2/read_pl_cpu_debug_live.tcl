set out [open "H:/testproject/project_2/read_pl_cpu_debug_live.out" "w"]
set dbg_base 0x41210000

proc read_reg {out name offset} {
    global dbg_base
    set value [mrd -value [expr {$dbg_base + $offset}]]
    puts $out [format "%s=0x%08x" $name $value]
}

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

puts $out "READ_PL_CPU_DEBUG_LIVE_BEGIN"
read_reg $out DBG_MAGIC 0x00
read_reg $out DBG_FLAGS 0x04
read_reg $out DBG_I_AR_COUNT 0x08
read_reg $out DBG_I_R_COUNT 0x0c
read_reg $out DBG_D_AW_COUNT 0x10
read_reg $out DBG_D_W_COUNT 0x14
read_reg $out DBG_D_B_COUNT 0x18
read_reg $out DBG_MMIO_AW_COUNT 0x1c
read_reg $out DBG_D_AR_COUNT 0x80
read_reg $out DBG_D_R_COUNT 0x84
read_reg $out DBG_LAST_I_ARADDR 0x20
read_reg $out DBG_LAST_I_PS_ARADDR 0x24
read_reg $out DBG_LAST_I_RDATA 0x28
read_reg $out DBG_LAST_D_AWADDR 0x2c
read_reg $out DBG_LAST_D_WDATA 0x30
read_reg $out DBG_LAST_D_ARADDR 0x88
read_reg $out DBG_LAST_D_PS_ARADDR 0x8c
read_reg $out DBG_LAST_D_RDATA 0x90
read_reg $out DBG_D_AWLEN_WLAST 0xd4
read_reg $out DBG_D_WLAST_COUNT 0xd8
read_reg $out DBG_IOP_AW_COUNT 0xdc
read_reg $out DBG_IOP_W_COUNT 0xe0
read_reg $out DBG_IOP_B_COUNT 0xe4
read_reg $out DBG_IOP_AR_COUNT 0xe8
read_reg $out DBG_IOP_R_COUNT 0xec
read_reg $out DBG_LAST_IOP_AWADDR 0xf0
read_reg $out DBG_LAST_IOP_WDATA 0xf4
read_reg $out DBG_IOP_LIVE_BITS 0xf8
read_reg $out DBG_CPU_PC 0xfc
read_reg $out DBG_LAST_MMIO_AWADDR 0x34
read_reg $out DBG_LAST_MMIO_WDATA 0x38
read_reg $out DBG_ALIVE 0x3c
read_reg $out DBG_I_RDATA0 0x40
read_reg $out DBG_I_RDATA1 0x44
read_reg $out DBG_I_RDATA2 0x48
read_reg $out DBG_I_RDATA3 0x4c
read_reg $out DBG_RESP_OR 0x50
catch {con}
close $out
disconnect
