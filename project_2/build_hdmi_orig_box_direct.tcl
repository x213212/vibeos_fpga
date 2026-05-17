set root_dir H:/testproject/project_2
set src_dir $root_dir/zynq_z7lite_training/Tutorial/part1/10.hdmi_simple/10.hdmi_simple.srcs/sources_1/new
set xdc_file $root_dir/zynq_z7lite_training/Tutorial/part1/10.hdmi_simple/10.hdmi_simple.srcs/constrs_1/new/top.xdc
set out_dir $root_dir/hdmi_orig_box_direct

file mkdir $out_dir

read_verilog [list \
    $src_dir/async_reset.v \
    $src_dir/display_clock.v \
    $src_dir/display_timings.v \
    $src_dir/dvi_generator.v \
    $src_dir/serializer_10to1.v \
    $src_dir/tmds_encoder_dvi.v \
    $root_dir/hdmi_orig_box.v \
]
read_xdc $xdc_file

synth_design -top hdmi_orig_box -part xc7z020clg400-1
opt_design
place_design
route_design
report_timing_summary -file $out_dir/hdmi_orig_box_timing_summary.rpt
report_drc -file $out_dir/hdmi_orig_box_drc.rpt
write_bitstream -force $out_dir/hdmi_orig_box.bit

puts "HDMI_ORIG_BOX_BIT=$out_dir/hdmi_orig_box.bit"
