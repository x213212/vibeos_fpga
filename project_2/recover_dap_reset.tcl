connect -url tcp:127.0.0.1:3121
puts "RECOVER_DAP_RESET_BEGIN"
puts [targets]
catch {targets -set -filter {name =~ "DAP*"}}
catch {rst -system} msg
puts "RST_SYSTEM=$msg"
after 2000
disconnect
