set out [open "H:/testproject/project_2/list_targets.out" "w"]
connect -url tcp:127.0.0.1:3121
puts $out [targets]
close $out
disconnect
