catch {connect arm hw -debugdevice cpunr 1} conn1
puts "CONNECT_ARM1=$conn1"
puts "TARGETS1_BEGIN"
catch {targets} targets1
puts $targets1
puts "TARGETS1_END"
catch {mrd 0xF8000000} mrd1
puts "MRD1=$mrd1"
catch {disconnect}

catch {connect arm hw -debugdevice cpunr 2} conn2
puts "CONNECT_ARM2=$conn2"
puts "TARGETS2_BEGIN"
catch {targets} targets2
puts $targets2
puts "TARGETS2_END"
catch {mrd 0xF8000000} mrd2
puts "MRD2=$mrd2"
catch {disconnect}
