open_hw_manager
connect_hw_server -url localhost:3121
set out [open "H:/testproject/project_2/check_hw_targets.out" "w"]
puts $out "CHECK_HW_TARGETS_BEGIN"
puts $out "SERVERS=[get_hw_servers]"
set targets [get_hw_targets *]
puts $out "TARGET_COUNT=[llength $targets]"
foreach t $targets {
    puts $out "TARGET=$t"
    catch {puts $out "  NAME=[get_property NAME $t]"}
    catch {puts $out "  STATE=[get_property STATE $t]"}
    catch {puts $out "  IS_OPEN=[get_property IS_OPENED $t]"}
}
if {[llength $targets] > 0} {
    current_hw_target [lindex $targets 0]
    catch {open_hw_target} open_res
    puts $out "OPEN_RESULT=$open_res"
    set devs [get_hw_devices *]
    puts $out "DEVICE_COUNT=[llength $devs]"
    foreach d $devs {
        puts $out "DEVICE=$d"
        catch {puts $out "  PART=[get_property PART $d]"}
        catch {puts $out "  IDCODE=[get_property IDCODE $d]"}
    }
} else {
    puts $out "NO_HW_TARGETS"
}
close $out
disconnect_hw_server
close_hw_manager
