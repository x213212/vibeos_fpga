param(
    [int]$Seconds = 30
)

& 'H:\Xilinx\vivado\Vivado\2019.2\bin\xsdb.bat' 'H:\testproject\project_2\read_jtag_console.tcl' $Seconds
