proc bits_to_int_lsb {bits} {
    set value 0
    set width [string length $bits]
    for {set bit 0} {$bit < $width} {incr bit} {
        if {[string index $bits $bit] eq "1"} {
            set value [expr {$value | (1 << $bit)}]
        }
    }
    return $value
}

proc reverse_bits {bits} {
    set rev ""
    for {set bit [expr {[string length $bits] - 1}]} {$bit >= 0} {incr bit -1} {
        append rev [string index $bits $bit]
    }
    return $rev
}

proc decode_console_dr {dr10} {
    set overflow [string index $dr10 9]
    set valid [string index $dr10 8]
    set seq_id 0
    set data_bits [string range $dr10 0 7]
    set data [bits_to_int_lsb $data_bits]

    if {[string length $dr10] >= 18} {
        set overflow [string index $dr10 17]
        set valid [string index $dr10 16]
        set seq_id [bits_to_int_lsb [string range $dr10 8 15]]
        set data_bits [string range $dr10 0 7]
        set data [bits_to_int_lsb $data_bits]
        return [list $dr10 $overflow $valid $data $seq_id]
    }

    if {$valid ne "1"} {
        set rev [reverse_bits $dr10]
        if {[string index $rev 8] eq "1"} {
            set dr10 $rev
            set overflow [string index $dr10 9]
            set valid [string index $dr10 8]
            set data_bits [string range $dr10 0 7]
            set data [bits_to_int_lsb $data_bits]
        }
    }

    return [list $dr10 $overflow $valid $data $seq_id]
}

proc poll_jtag_console {{poll_seconds 30} {poll_ms 1}} {
    connect -url tcp:127.0.0.1:3121
    jtag targets -set -filter {level == 0}
    jtag targets -open -filter {level == 0}
    after 1000
    jtag targets -set -filter {name =~ "xc7z020*" && irlen == 6}

    set seq [jtag sequence]
    set start_ms [clock milliseconds]
    set timeout_ms [expr {$poll_seconds * 1000}]

    # 7-series USER1 opcode is 6'b000010.
    # With XSDB "-bits", the first character is shifted first, so use "010000".
    set user1_ir_bits "010000"
    set dr_width 18
    set dr_ack_width 10

    set raw_debug 0
    if {[info exists ::env(JTAG_RAW_DEBUG)]} {
        set raw_debug $::env(JTAG_RAW_DEBUG)
    }
    set raw_probe 0
    if {[info exists ::env(JTAG_RAW_PROBE)]} {
        set raw_probe $::env(JTAG_RAW_PROBE)
        set raw_debug 1
    }
    set dr_ack_bits "1001111001"
    set dr_read_bits [string repeat "0" $dr_width]
    if {$raw_probe} {
        set dr_read_bits [string repeat "0" $dr_width]
        set dr_ack_bits $dr_read_bits
    }
    set raw_limit 40
    if {[info exists ::env(JTAG_RAW_LIMIT)]} {
        set raw_limit $::env(JTAG_RAW_LIMIT)
    }
    set raw_count 0

    puts "JTAG_CONSOLE_BEGIN"
    flush stdout
    $seq clear
    $seq state RESET
    $seq state IDLE

    set last_seq -1
    while {[expr {[clock milliseconds] - $start_ms}] < $timeout_ms} {
        $seq clear
        $seq state IDLE
        $seq irshift -bits -state IDLE 6 $user1_ir_bits
        $seq drshift -capture -bits -state IDLE $dr_width $dr_read_bits
        set dr [$seq run -bits -single]
        set dr10 [string range $dr 0 [expr {$dr_width - 1}]]

        lassign [decode_console_dr $dr10] decoded overflow valid data seq_id
        set empty [expr {$valid eq "1" ? 0 : 1}]

        if {$raw_debug && ($raw_probe || $raw_count < $raw_limit)} {
            puts [format "RAW_DR=%s DECODED=%s overflow=%s valid=%s empty=%d seq=0x%02x data=0x%02x" \
                $dr10 $decoded $overflow $valid $empty $seq_id $data]
            incr raw_count
            flush stdout
        }

        if {!$raw_probe && $valid eq "1"} {
            set last_seq $seq_id
            puts -nonewline [format "%c" $data]
            flush stdout
            after 5
        }

        if {$overflow eq "1"} {
            puts "\n\[JTAG console overflow\]"
            flush stdout
        }

        after $poll_ms
    }

    $seq delete
    disconnect
}

set seconds 30
if {$argc >= 1} {
    set seconds [lindex $argv 0]
}

poll_jtag_console $seconds
