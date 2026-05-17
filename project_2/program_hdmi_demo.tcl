connect
targets -set -filter {name =~ "xc7z020*"}
fpga H:/testproject/project_2/hdmi_demo_hw/hdmi_demo_hw.runs/impl_1/hdmi_demo.bit
puts "HDMI_DEMO_PROGRAMMED"
