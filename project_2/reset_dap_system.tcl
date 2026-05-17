connect -url tcp:127.0.0.1:3121
catch {targets -set -filter {name =~ "DAP*"}}
catch {rst -system}
after 1000
targets
disconnect
