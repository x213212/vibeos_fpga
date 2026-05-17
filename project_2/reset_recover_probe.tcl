proc show_targets {label} {
    puts $label
    puts [targets]
    flush stdout
}

proc try_mrd_slcr {label} {
    catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}} target_set
    catch {mrd 0xF8007080} mrd_res
    puts "$label target_set=$target_set mrd=$mrd_res"
    flush stdout
}

connect
show_targets "TARGETS_INITIAL"

catch {targets -set -filter {name =~ "DAP*"}}
catch {rst -dap} dap_rst
puts "RST_DAP=$dap_rst"
catch {disconnect}
after 1000
connect
show_targets "TARGETS_AFTER_DAP"
try_mrd_slcr "AFTER_DAP"

catch {targets -set -filter {name =~ "APU*"}}
catch {rst -system} sys_rst
puts "RST_SYSTEM=$sys_rst"
catch {disconnect}
after 1500
connect
show_targets "TARGETS_AFTER_SYSTEM"
try_mrd_slcr "AFTER_SYSTEM"

catch {targets -set -filter {name =~ "APU*"}}
catch {rst -srst} srst_rst
puts "RST_SRST=$srst_rst"
catch {disconnect}
after 2000
connect
show_targets "TARGETS_AFTER_SRST"
try_mrd_slcr "AFTER_SRST"

catch {targets -set -filter {name =~ "APU*"}}
catch {rst -por} por_rst
puts "RST_POR=$por_rst"
catch {disconnect}
after 3000
connect
show_targets "TARGETS_AFTER_POR"
try_mrd_slcr "AFTER_POR"

disconnect
