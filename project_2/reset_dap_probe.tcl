connect
puts "BEFORE"
puts [targets]
catch {targets -set -filter {name =~ "DAP*"}} dap_set
puts "DAP_SET=$dap_set"
catch {rst -dap} dap_rst
puts "DAP_RST=$dap_rst"
after 1000
catch {disconnect}
after 1000
connect
puts "AFTER_RST_DAP"
puts [targets]
catch {targets -set -filter {name =~ "DAP*"}} dap_set2
puts "DAP_SET2=$dap_set2"
catch {mrd 0xF8000000} mrd_res
puts "MRD_SLCR=$mrd_res"
disconnect
