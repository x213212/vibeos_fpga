set root_dir H:/testproject/project_2
set src_dir $root_dir/zynq_z7lite_training/Tutorial/part1/10.hdmi_simple/10.hdmi_simple.srcs/sources_1/new
set xdc_file $root_dir/zynq_z7lite_training/Tutorial/part1/10.hdmi_simple/10.hdmi_simple.srcs/constrs_1/new/top.xdc
set out_dir $root_dir/hdmi_orig_box_hw

file delete -force $out_dir
create_project hdmi_orig_box_hw $out_dir -part xc7z020clg400-1 -force
add_files -norecurse [list \
    $src_dir/async_reset.v \
    $src_dir/display_clock.v \
    $src_dir/display_timings.v \
    $src_dir/dvi_generator.v \
    $src_dir/serializer_10to1.v \
    $src_dir/tmds_encoder_dvi.v \
    $root_dir/hdmi_orig_box.v \
]
add_files -fileset constrs_1 -norecurse $xdc_file
set_property top hdmi_orig_box [current_fileset]
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

puts "HDMI_ORIG_BOX_BIT=$out_dir/hdmi_orig_box_hw.runs/impl_1/hdmi_orig_box.bit"
close_project
