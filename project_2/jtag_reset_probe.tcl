connect
puts "JTAG_BEFORE"
puts [jtag targets]
catch {jtag targets -set -filter {name =~ "arm_dap*"}} arm_jtag_set
puts "ARM_JTAG_SET=$arm_jtag_set"
set seq [jtag sequence]
$seq state RESET
$seq state IDLE
catch {$seq run} seq_res
puts "SEQ_RUN=$seq_res"
$seq delete
catch {disconnect}
after 1000
connect
puts "TARGETS_AFTER_JTAG_RESET"
puts [targets]
catch {targets -set -filter {name =~ "DAP*"}} dap_set
puts "DAP_SET=$dap_set"
catch {mrd 0xF8000000} mrd_res
puts "MRD_SLCR=$mrd_res"
disconnect
