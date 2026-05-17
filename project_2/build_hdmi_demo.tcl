set root_dir H:/testproject/project_2
set out_dir $root_dir/hdmi_demo_hw

file delete -force $out_dir
create_project hdmi_demo_hw $out_dir -part xc7z020clg400-1 -force
add_files -norecurse $root_dir/hdmi_demo.v
add_files -fileset constrs_1 -norecurse $root_dir/hdmi_demo.xdc
set_property top hdmi_demo [current_fileset]
update_compile_order -fileset sources_1

launch_runs synth_1 -jobs 2
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] != "100%"} {
    error "synth_1 did not finish"
}

launch_runs impl_1 -to_step write_bitstream -jobs 2
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] != "100%"} {
    error "impl_1 did not finish"
}

puts "HDMI_DEMO_BIT=$out_dir/hdmi_demo_hw.runs/impl_1/hdmi_demo.bit"
close_project
