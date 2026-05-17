open_project H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.xpr
open_bd_design H:/testproject/project_2/riscv_ps_ddr_hw/riscv_ps_ddr_hw.srcs/sources_1/bd/riscv_ps_ddr/riscv_ps_ddr.bd

puts "ADDR_SPACES"
foreach sp [get_bd_addr_spaces] {
    puts "SPACE $sp"
    foreach seg [get_bd_addr_segs -of_objects $sp] {
        set offset [get_property OFFSET $seg]
        set range [get_property RANGE $seg]
        puts "  SEG $seg OFFSET=$offset RANGE=$range"
    }
}

puts "HP0 SEGMENTS"
foreach seg [get_bd_addr_segs -hierarchical *HP0*] {
    set offset [get_property OFFSET $seg]
    set range [get_property RANGE $seg]
    puts "  SEG $seg OFFSET=$offset RANGE=$range"
}

close_project
