$out = "H:\testproject\project_2\ddr_read_probe.bin"

function Add-WordLE {
    param([System.Collections.Generic.List[byte]]$Bytes, [Int64]$Word)
    $Word = $Word -band 0xffffffff
    $Bytes.Add([byte]($Word -band 0xff))
    $Bytes.Add([byte](($Word -shr 8) -band 0xff))
    $Bytes.Add([byte](($Word -shr 16) -band 0xff))
    $Bytes.Add([byte](($Word -shr 24) -band 0xff))
}

function IType([int]$imm, [int]$rs1, [int]$funct3, [int]$rd, [int]$op) {
    return ((($imm -band 0xfff) -shl 20) -bor ($rs1 -shl 15) -bor ($funct3 -shl 12) -bor ($rd -shl 7) -bor $op) -band 0xffffffff
}

function SType([int]$imm, [int]$rs2, [int]$rs1, [int]$funct3, [int]$op) {
    $lo = $imm -band 0x1f
    $hi = ($imm -shr 5) -band 0x7f
    return (($hi -shl 25) -bor ($rs2 -shl 20) -bor ($rs1 -shl 15) -bor ($funct3 -shl 12) -bor ($lo -shl 7) -bor $op) -band 0xffffffff
}

function UType([int]$imm20, [int]$rd, [int]$op) {
    return (($imm20 -shl 12) -bor ($rd -shl 7) -bor $op) -band 0xffffffff
}

function JType([int]$imm, [int]$rd) {
    $u = $imm -band 0x1fffff
    return ((($u -shr 20) -shl 31) -bor (((($u -shr 1) -band 0x3ff)) -shl 21) -bor (((($u -shr 11) -band 1)) -shl 20) -bor (((($u -shr 12) -band 0xff)) -shl 12) -bor ($rd -shl 7) -bor 0x6f) -band 0xffffffff
}

$bytes = [System.Collections.Generic.List[byte]]::new()

# x5=t0 source pointer, x6=t1 loaded word, x7=t2 MMIO base.
$words = @(
    0x00000013,                 # nop
    0x00000013,                 # nop
    (UType 0x80000 5 0x37),     # lui t0, 0x80000
    (IType 128 5 0 5 0x13),     # addi t0, t0, 0x80
    (IType 0 5 2 6 0x03),       # lw t1, 0(t0)
    (UType 0x10000 7 0x37),     # lui t2, 0x10000
    (SType 0 6 7 2 0x23),       # sw t1, 0(t2)
    (JType 0 0)                 # j .
)

foreach ($w in $words) {
    Add-WordLE $bytes ([Int64]$w)
}

while ($bytes.Count -lt 128) {
    $bytes.Add(0)
}
[byte[]]$msg = [Text.Encoding]::ASCII.GetBytes("Hello OS!`n`0")
$bytes.AddRange($msg)

while ($bytes.Count -lt 4096) {
    $bytes.Add(0)
}
[IO.File]::WriteAllBytes($out, $bytes.ToArray())
Write-Host ("WROTE {0} bytes to {1}" -f $bytes.Count, $out)
