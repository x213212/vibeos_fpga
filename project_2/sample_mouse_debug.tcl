set out [open "H:/testproject/project_2/sample_mouse_debug.out" "w"]

connect -url tcp:127.0.0.1:3121
configparams force-mem-access 1
catch {targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}}

puts $out "SAMPLE_MOUSE_DEBUG_BEGIN"
puts $out "columns: sample state dbg1 dbg2 dbg3 dbg6 dbg7 usbcmd usbsts frindex portsc1"

for {set i 0} {$i < 80} {incr i} {
    set state 0
    set dbg1 0
    set dbg2 0
    set dbg3 0
    set dbg6 0
    set dbg7 0
    set usbcmd 0
    set usbsts 0
    set frindex 0
    set portsc1 0
    catch {set state [mrd -value 0x412100b0]}
    catch {set dbg1 [mrd -value 0x412100b8]}
    catch {set dbg2 [mrd -value 0x412100bc]}
    catch {set dbg3 [mrd -value 0x412100c0]}
    catch {set dbg6 [mrd -value 0x412100cc]}
    catch {set dbg7 [mrd -value 0x412100d0]}
    catch {set usbcmd [mrd -value 0xe0002140]}
    catch {set usbsts [mrd -value 0xe0002144]}
    catch {set frindex [mrd -value 0xe000214c]}
    catch {set portsc1 [mrd -value 0xe0002184]}
    puts $out [format "%02d 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x" \
        $i $state $dbg1 $dbg2 $dbg3 $dbg6 $dbg7 $usbcmd $usbsts $frindex $portsc1]
    flush $out
    after 100
}

puts $out "SAMPLE_MOUSE_DEBUG_END"
close $out
disconnect
