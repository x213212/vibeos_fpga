create_project check_ifx H:/testproject/project_2/check_ifx_tmp -part xc7z020clg400-1 -force
puts "IFX_DIR=[::bd::get_vlnv_dir xilinx.com:ip:ifx_util:1.1]"
puts "IFX_DEBUG_EXISTS=[file exists [file join [::bd::get_vlnv_dir xilinx.com:ip:ifx_util:1.1] bd ifx_common_debug_util.tcl]]"
exit
