set root_dir H:/testproject
set out_dir $root_dir/project_2/riscv_ps_ddr_hw
set checkpoint_dir $root_dir/project_2/checkpoints
set incremental_checkpoint $checkpoint_dir/riscv_ps_ddr_last_route.dcp
set build_jobs 8
set synth_only 0
set impl_only 0

for {set i 0} {$i < [llength $argv]} {incr i} {
    set arg [lindex $argv $i]
    if {$arg eq "-jobs"} {
        incr i
        if {$i >= [llength $argv]} {
            error "Missing value for -jobs"
        }
        set build_jobs [lindex $argv $i]
    } elseif {$arg eq "-impl_only"} {
        set impl_only 1
    } elseif {$arg eq "-synth_only"} {
        set synth_only 1
    } else {
        error "Unknown argument: $arg"
    }
}

set xpr $out_dir/riscv_ps_ddr_hw.xpr
if {![file exists $xpr]} {
    error "Project not found: $xpr. Run build_riscv_ps_ddr_hw.tcl once first."
}

set_param general.maxThreads $build_jobs
file mkdir $checkpoint_dir

open_project $xpr
update_compile_order -fileset sources_1

if {!$impl_only} {
    reset_run synth_1
    launch_runs synth_1 -jobs $build_jobs
    wait_on_run synth_1
}

if {!$synth_only} {
    if {[file exists $incremental_checkpoint]} {
        puts "USING_INCREMENTAL_CHECKPOINT=$incremental_checkpoint"
        set_property incremental_checkpoint $incremental_checkpoint [get_runs impl_1]
    } else {
        puts "NO_INCREMENTAL_CHECKPOINT"
    }

    reset_run impl_1
    launch_runs impl_1 -to_step write_bitstream -jobs $build_jobs
    wait_on_run impl_1

    set routed_dcp $out_dir/riscv_ps_ddr_hw.runs/impl_1/riscv_ps_ddr_wrapper_routed.dcp
    if {[file exists $routed_dcp]} {
        file copy -force $routed_dcp $incremental_checkpoint
        puts "SAVED_INCREMENTAL_CHECKPOINT=$incremental_checkpoint"
    }

    write_hwdef -force -file $out_dir/riscv_ps_ddr.hwdef
}

close_project
