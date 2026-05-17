set out [open "H:/testproject/project_2/recover_dap.out" "w"]
connect -url tcp:127.0.0.1:3121
puts $out "RECOVER_DAP_BEGIN"
puts $out "targets before:"
puts $out [targets]
if {[catch {rst -system} e]} {
    puts $out "rst_system_error=$e"
}
after 1000
puts $out "targets after:"
puts $out [targets]
close $out
disconnect
